// Exact dielectric Fresnel reflectance for an air/glass interface.
uniform float uGlassThickness;
float glassFresnel(float cosine, float ior){
    float transmitted = sqrt(max(0.0, 1.0 - (1.0 - cosine * cosine) / (ior * ior)));
    float perpendicular = (cosine - ior * transmitted) / max(cosine + ior * transmitted, 1e-6);
    float parallel = (ior * cosine - transmitted) / max(ior * cosine + transmitted, 1e-6);
    return 0.5 * (perpendicular * perpendicular + parallel * parallel);
}

// Refracts through both sphere interfaces, sampling a separate copy of the visible scene.
// Background lookup projects the exit ray a fixed distance; offscreen rays use the environment.
vec3 glassColor(vec3 tint, vec3 normal){
    vec3 incident = normalize(worldPosition - uEye);
    vec3 inside = refract(incident, normal, 1.0 / uIor);
    float distance = max(0.0, -2.0 * dot(worldPosition - uGlassSphere.xyz, inside));
    vec3 exitPosition = worldPosition + inside * distance;
    vec3 exitNormal = normalize(exitPosition - uGlassSphere.xyz);
    if (uGlassThickness>0.0){
        // Finite circular slab: both parallel faces and its cylindrical edge.
        float cap=1e20;
        if (abs(inside.y)>1e-6){
            float y=uGlassSphere.y+sign(inside.y)*uGlassThickness*.5;
            cap=max(0.0,(y-worldPosition.y)/inside.y);
        }
        vec2 offset=worldPosition.xz-uGlassSphere.xz;
        float a=dot(inside.xz,inside.xz),b=dot(offset,inside.xz);
        float c=dot(offset,offset)-uGlassSphere.w*uGlassSphere.w;
        float edge=a>1e-8 ? max(0.0,(-b+sqrt(max(0.0,b*b-a*c)))/a) : 1e20;
        distance=min(cap,edge);
        exitPosition=worldPosition+inside*distance;
        exitNormal=cap<=edge ? vec3(0,sign(inside.y),0)
            : normalize(vec3(exitPosition.x-uGlassSphere.x,0,exitPosition.z-uGlassSphere.z));
    }
    vec3 transmitted = refract(inside, -exitNormal, uIor);
    if (dot(transmitted, transmitted) < 1e-6){
        vec3 reflection = uEnvironmentEnabled
            ? reflectedEnvironment(reflect(incident, normal), 0, uReflectionProbe,worldPosition) : vec3(0);
        return uLinearOutput ? reflection : displayColor(reflection);
    }
    vec4 projected = uProjection * uView * vec4(exitPosition + transmitted * uGlassSphere.w * 3.0, 1);
    vec2 uv = projected.xy / max(projected.w, 1e-5) * 0.5 + 0.5;
    bool onScreen = projected.w > 0.0 && all(greaterThanEqual(uv, vec2(0))) &&
                    all(lessThanEqual(uv, vec2(1)));
    vec3 through = onScreen ? texture(uTextures[3], uv).rgb
                            : uEnvironmentEnabled ? environmentRadiance(transmitted, 0, uReflectionProbe,exitPosition)
                                                  : vec3(0);
    vec3 reflected = uEnvironmentEnabled
        ? reflectedEnvironment(reflect(incident, normal),
                                uMaterial.roughness * uEnvironmentMaxLod, uReflectionProbe,worldPosition) : vec3(0);
    vec3 absorption = pow(clamp(tint, 0.001, 1.0), vec3(distance));
    if (!uLinearOutput){
        if (!onScreen){ through = displayColor(through); }
        through = pow(pow(max(through, vec3(0)), vec3(2.2)) * absorption, vec3(1.0 / 2.2));
        reflected = displayColor(reflected);
    } else{
        through *= absorption;
    }
    float fresnel = glassFresnel(clamp(dot(-incident, normal), 0.0, 1.0), uIor);
    float reflection = 2.0 * fresnel / (1.0 + fresnel);
    return mix(through, reflected, reflection);
}
