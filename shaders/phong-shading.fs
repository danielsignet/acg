#version 410 core
in vec3 worldPosition, worldNormal;
in vec2 texCoord;
out vec4 fragmentColor;

struct Material{
    vec3 ambient, diffuse, specular;
    float shininess;
};

struct Light{
    vec4 position;
    vec3 ambient, diffuse, specular, attenuation;
    bool enabled;
};

uniform Material uMaterial;
uniform Light uLights[8];
uniform int uLightCount;
uniform vec3 uEye, uTint, uEmissive;
uniform bool uLighting, uToon, uMultiTexture, uFog;
uniform sampler2D uTextures[4];
uniform bool uHasTexture[4];
uniform float uTextureMix, uAlphaCutoff, uOpacity;

uniform sampler2D uSunShadow;
uniform samplerCube uPointShadow;
uniform mat4 uSunVP;
uniform mat3 uSunPlane;
uniform bool uShadows, uReceiveShadows, uShadowPCF;
uniform int uSunShadowIndex, uPointShadowIndex;
uniform float uPointShadowFar;

float sunVisibility(vec3 normal){
    vec4 clip = uSunVP * vec4(worldPosition, 1.0);
    vec3 p = clip.xyz / clip.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0))) || any(greaterThan(p, vec3(1)))){
        return 1.0;
    }
    float bias = 0.00004;
    vec3 plane = uSunPlane * normal;
    vec2 gradient = abs(plane.z) > 1e-6 ? -plane.xy / plane.z : vec2(0);
    vec2 texel = 1.0 / vec2(textureSize(uSunShadow, 0));
    float visible = 0.0;
    int radius = uShadowPCF ? 1 : 0;
    for (int y = -radius; y <= radius; ++y){
        for (int x = -radius; x <= radius; ++x){
            vec2 uv = (floor((p.xy + vec2(x, y) * texel) / texel) + 0.5) * texel;
            float reference = p.z + dot(gradient, uv - p.xy);
            visible += reference - bias <= texture(uSunShadow, uv).r ? 1.0 : 0.0;
        }
    }
    return visible / float((2 * radius + 1) * (2 * radius + 1));
}

vec3 cubeTexelDirection(vec3 direction){
    vec3 a = abs(direction);
    vec2 uv;
    int face;
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
    float size = float(textureSize(uPointShadow, 0).x);
    uv =
        (clamp(floor((uv * 0.5 + 0.5) * size), vec2(0), vec2(size - 1.0)) + 0.5) / size * 2.0 - 1.0;
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

float pointVisibility(vec3 normal, vec3 lightPosition){
    vec3 delta = worldPosition - lightPosition;
    float distanceToLight = length(delta);
    if (distanceToLight >= uPointShadowFar){
        return 1.0;
    }
    float bias = 0.01;
    vec3 ray = normalize(delta);
    vec3 axis = abs(ray.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(ray, axis)), bitangent = cross(ray, tangent);
    float texelRadius = 2.0 * distanceToLight / float(textureSize(uPointShadow, 0).x),
          visible = 0.0;
    int radius = uShadowPCF ? 1 : 0;
    for (int y = -radius; y <= radius; ++y){
        for (int x = -radius; x <= radius; ++x){
            vec3 sampleRay = cubeTexelDirection(
                delta + (tangent * float(x) + bitangent * float(y)) * texelRadius);
            float denominator = dot(normal, sampleRay), numerator = dot(normal, delta);
            float reference = abs(denominator) > 1e-6 && numerator * denominator > 0.0
                                  ? numerator / denominator * length(sampleRay)
                                  : distanceToLight;
            float stored = texture(uPointShadow, sampleRay).r * uPointShadowFar;
            visible += reference - bias <= stored ? 1.0 : 0.0;
        }
    }
    return visible / float((2 * radius + 1) * (2 * radius + 1));
}

// Shades the surface and discards fragments below the alpha threshold.
void main(){
    vec4 surface = vec4(uMaterial.diffuse * uTint, 1.0);
    if (uHasTexture[0]){
        surface *= texture(uTextures[0], texCoord);
    }
    if (surface.a < uAlphaCutoff){
        discard;
    }
    if (uMultiTexture){
        for (int i = 1; i < 4; ++i){
            if (uHasTexture[i]){
                surface.rgb *= mix(vec3(1.0), texture(uTextures[i], texCoord).rgb, uTextureMix);
            }
        }
    }
    vec3 n = normalize(worldNormal);
    vec3 planeNormal = cross(dFdx(worldPosition), dFdy(worldPosition));
    planeNormal = length(planeNormal) > 1e-8 ? normalize(planeNormal) : n;
    if (!gl_FrontFacing){
        n = -n;
    }
    vec3 viewDirection = normalize(uEye - worldPosition);
    vec3 result = surface.rgb;
    if (uLighting){
        result = uMaterial.ambient * surface.rgb * 0.6;
        for (int i = 0; i < uLightCount; ++i){
            if (!uLights[i].enabled){
                continue;
            }
            vec3 lightDirection;
            float attenuation = 1.0;
            if (uLights[i].position.w == 0.0){
                lightDirection = normalize(uLights[i].position.xyz);
            } else{
                vec3 delta = uLights[i].position.xyz - worldPosition;
                float d = length(delta);
                lightDirection = delta / max(d, 0.0001);
                attenuation = 1.0 / max(dot(uLights[i].attenuation, vec3(1.0, d, d * d)), 0.0001);
            }
            float diffuse = max(dot(n, lightDirection), 0.0);
            float specular = 0.0;
            if (diffuse > 0.0){
                specular = pow(max(dot(viewDirection, reflect(-lightDirection, n)), 0.0),
                               uMaterial.shininess);
            }
            if (uToon){
                diffuse = floor(diffuse * 4.0) / 4.0;
                specular = step(0.45, specular);
            }
            float visibility = 1.0;
            if (uShadows && uReceiveShadows && diffuse > 0.0){
                if (i == uSunShadowIndex){
                    visibility = sunVisibility(planeNormal);
                } else if (i == uPointShadowIndex){
                    visibility = pointVisibility(planeNormal, uLights[i].position.xyz);
                }
            }
            result +=
                attenuation * (surface.rgb * uLights[i].ambient +
                               visibility * (surface.rgb * uLights[i].diffuse * diffuse +
                                             uMaterial.specular * uLights[i].specular * specular));
        }
        if (uToon){
            result *= smoothstep(0.05, 0.3, abs(dot(n, viewDirection)));
        }
    }
    result += uEmissive;
    vec3 mapped = pow(result / (result + vec3(0.65)), vec3(1.0 / 2.2));
    float fog = uFog ? 1.0 - exp(-0.00055 * dot(uEye - worldPosition, uEye - worldPosition)) : 0.0;
    fragmentColor =
        vec4(mix(mapped, vec3(0.035, 0.052, 0.078), clamp(fog, 0.0, 0.82)), surface.a * uOpacity);
}
