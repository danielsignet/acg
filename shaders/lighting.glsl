struct Material{
    vec3 ambient, diffuse, specular;
    float shininess;
};

struct Light{
    vec4 position;
    vec3 ambient, diffuse, specular, attenuation;
    vec3 direction;
    float innerCos, outerCos, range;
    bool enabled, spotlight;
};

uniform Light uLights[8];
uniform int uLightCount;
uniform vec3 uEye;
uniform sampler2D uSunShadow;
uniform samplerCube uPointShadows[8];
uniform mat4 uSunVP;
uniform mat3 uSunPlane;
uniform bool uShadows, uReceiveShadows, uShadowPCF;
uniform int uSunShadowIndex, uPointShadowSlot[8];
uniform float uPointShadowFar[8];

// Interpolates a three-texel PCF kernel over four texel centers per axis.
float shadowFilterWeight(int offset, float fraction){
    return offset == -1 ? 1.0 - fraction : offset == 2 ? fraction : 1.0;
}

float sunVisibility(vec3 position, vec3 normal){
    vec4 clip = uSunVP * vec4(position, 1.0);
    vec3 p = clip.xyz / clip.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0))) || any(greaterThan(p, vec3(1)))){
        return 1.0;
    }
    vec3 plane = uSunPlane * normal;
    vec2 gradient = abs(plane.z) > 1e-6 ? -plane.xy / plane.z : vec2(0);
    vec2 texel = 1.0 / vec2(textureSize(uSunShadow, 0));
    float visible = 0.0;
    int radius = uShadowPCF ? 1 : 0;
    // Cover the filter footprint where adjacent faces invalidate plane extrapolation.
    vec3 lightDirection = normalize(uLights[uSunShadowIndex].position.xyz);
    float grazing = clamp(2.0 * (1.0 - abs(dot(normal, lightDirection))), 0.0, 1.0);
    float footprint = uShadowPCF ? 2.0 : 0.5;
    float bias = 0.00004 + dot(abs(gradient), texel) * footprint * grazing * grazing;
    vec2 center = p.xy / texel - 0.5;
    vec2 fraction = fract(center);
    int end = uShadowPCF ? 2 : 0;
    for (int y = -radius; y <= end; ++y){
        for (int x = -radius; x <= end; ++x){
            vec2 uv = uShadowPCF ? (floor(center) + vec2(x, y) + 0.5) * texel
                                : (floor(p.xy / texel) + 0.5) * texel;
            float weight = shadowFilterWeight(x, fraction.x) * shadowFilterWeight(y, fraction.y);
            float reference = p.z + dot(gradient, uv - p.xy);
            visible += reference - bias <= texture(uSunShadow, uv).r ? weight : 0.0;
        }
    }
    return visible / float((2 * radius + 1) * (2 * radius + 1));
}

// Maps a ray to one cubemap face without changing its sampling position.
vec2 cubeCoordinates(vec3 direction, out int face){
    vec3 a = abs(direction);
    vec2 uv;
    if (a.x >= a.y && a.x >= a.z){
        if (direction.x > 0.0){
            face = 0;
            uv = vec2(-direction.z, -direction.y) / a.x;
        } else{
            face = 1;
            uv = vec2(direction.z, -direction.y) / a.x;
        }
    } else if (a.y >= a.z){
        if (direction.y > 0.0){
            face = 2;
            uv = vec2(direction.x, direction.z) / a.y;
        } else{
            face = 3;
            uv = vec2(direction.x, -direction.z) / a.y;
        }
    } else{
        if (direction.z > 0.0){
            face = 4;
            uv = vec2(direction.x, -direction.y) / a.z;
        } else{
            face = 5;
            uv = vec2(-direction.x, -direction.y) / a.z;
        }
    }
    return uv;
}

// Allows filter taps to cross face boundaries using their actual world-space rays.
vec3 cubeDirection(vec2 uv, int face){
    if (face == 0){
        return vec3(1, -uv.y, -uv.x);
    }
    if (face == 1){
        return vec3(-1, -uv.y, uv.x);
    }
    if (face == 2){
        return vec3(uv.x, 1, uv.y);
    }
    if (face == 3){
        return vec3(uv.x, -1, -uv.y);
    }
    if (face == 4){
        return vec3(uv.x, -uv.y, 1);
    }
    return vec3(-uv.x, -uv.y, -1);
}

// Finds the stored texel center, including taps crossing into an adjacent face.
vec3 cubeTexelDirection(vec3 direction, float size){
    int face;
    vec2 uv = cubeCoordinates(direction, face);
    uv = (clamp(floor((uv * 0.5 + 0.5) * size), vec2(0), vec2(size - 1.0)) + 0.5) / size *
             2.0 - 1.0;
    return cubeDirection(uv, face);
}

float pointVisibility(vec3 position, vec3 normal, vec3 lightPosition, int slot){
    vec3 delta = position - lightPosition;
    float distanceToLight = length(delta);
    if (distanceToLight < 0.0001 || distanceToLight >= uPointShadowFar[slot]){
        return 1.0;
    }
    float size = float(textureSize(uPointShadows[slot], 0).x), visible = 0.0;
    // Scale contact-edge tolerance with the light-space footprint and receiver slope.
    float cosine = clamp(abs(dot(normal, delta / max(distanceToLight, 0.0001))), 0.0, 1.0);
    float slope = sqrt(1.0 - cosine * cosine) / max(cosine, 0.1);
    float footprint = uShadowPCF ? 2.0 : 0.5;
    float bias = 0.01 + 2.0 * distanceToLight / size * footprint * slope;
    int face;
    vec2 center = (cubeCoordinates(delta, face) * 0.5 + 0.5) * size - 0.5;
    vec2 fraction = fract(center);
    int radius = uShadowPCF ? 1 : 0;
    int end = uShadowPCF ? 2 : 0;
    for (int y = -radius; y <= end; ++y){
        for (int x = -radius; x <= end; ++x){
            vec2 uv = (floor(center) + vec2(x, y) + 0.5) / size * 2.0 - 1.0;
            vec3 sampleRay = cubeTexelDirection(uShadowPCF ? cubeDirection(uv, face) : delta, size);
            float weight = shadowFilterWeight(x, fraction.x) * shadowFilterWeight(y, fraction.y);
            float denominator = dot(normal, sampleRay), numerator = dot(normal, delta);
            float reference = abs(denominator) > 1e-6 && numerator * denominator > 0.0
                                  ? numerator / denominator * length(sampleRay)
                                  : distanceToLight;
            float stored = texture(uPointShadows[slot], sampleRay).r * uPointShadowFar[slot];
            visible += reference - bias <= stored ? weight : 0.0;
        }
    }
    return visible / float((2 * radius + 1) * (2 * radius + 1));
}

// Evaluates the same world-space lighting for forward and deferred surfaces.
vec3 shadeSurface(vec3 position, vec3 n, vec3 planeNormal, vec4 surface, Material material,
                  vec3 emissive, bool lighting, bool toon, bool receiveShadows, int lightMask, float ambientVisibility){
    vec3 viewDirection = normalize(uEye - position);
    vec3 result = surface.rgb;
    if (lighting){
        result = material.ambient * surface.rgb * 0.6 * ambientVisibility;
        for (int i = 0; i < uLightCount; ++i){
            if (!uLights[i].enabled || (lightMask & (1 << i)) == 0){
                continue;
            }
            vec3 lightDirection;
            float attenuation = 1.0;
            if (uLights[i].position.w == 0.0){
                lightDirection = normalize(uLights[i].position.xyz);
            } else{
                vec3 delta = uLights[i].position.xyz - position;
                float d = length(delta);
                lightDirection = delta / max(d, 0.0001);
                attenuation = 1.0 / max(dot(uLights[i].attenuation, vec3(1.0, d, d * d)), 0.0001);
                if (uLights[i].range > 0.0){
                    attenuation *= 1.0 - smoothstep(uLights[i].range * 0.8, uLights[i].range, d);
                }
                if (uLights[i].spotlight){
                    attenuation *= smoothstep(uLights[i].outerCos, uLights[i].innerCos,
                                              dot(-lightDirection, uLights[i].direction));
                }
                if (attenuation == 0.0){
                    continue;
                }
            }
            float diffuse = max(dot(n, lightDirection), 0.0);
            float specular = 0.0;
            if (diffuse > 0.0){
                specular = pow(max(dot(viewDirection, reflect(-lightDirection, n)), 0.0),
                               material.shininess);
            }
            if (toon){
                diffuse = floor(diffuse * 4.0) / 4.0;
                specular = step(0.45, specular);
            }
            float visibility = 1.0;
            if (uShadows && receiveShadows){
                if (i == uSunShadowIndex){
                    visibility = sunVisibility(position, planeNormal);
                } else if (uPointShadowSlot[i] >= 0){
                    visibility = pointVisibility(position, planeNormal, uLights[i].position.xyz,
                                                 uPointShadowSlot[i]);
                }
            }
            result +=
                attenuation *
                (surface.rgb * uLights[i].ambient * ambientVisibility +
                 visibility * (surface.rgb * uLights[i].diffuse * diffuse +
                               material.specular * uLights[i].specular * specular));
        }
        if (toon){
            result *= smoothstep(0.05, 0.3, abs(dot(n, viewDirection)));
        }
    }
    result += emissive;

    return result;
}

// Maps linear lighting to the display while retaining the existing distance fog.
vec3 displaySurface(vec3 radiance, vec3 position, bool fogEnabled){
    vec3 mapped =
        pow(max(radiance, vec3(0)) / (max(radiance, vec3(0)) + vec3(0.65)), vec3(1.0 / 2.2));
    float fog = fogEnabled ? 1.0 - exp(-0.00055 * dot(uEye - position, uEye - position)) : 0.0;
    return mix(mapped, vec3(0.035, 0.052, 0.078), clamp(fog, 0.0, 0.82));
}
