uniform samplerCubeArray uEnvironment;
uniform sampler2D uEnvironmentBrdf;
uniform vec3 uIrradiance[9];
uniform float uEnvironmentMaxLod;
uniform int uReflectionMode, uReflectionReadyMask;

// Falls back to the HDRI until all six faces of a local probe have been published.
vec3 environmentRadiance(vec3 direction, float lod, int probe){
    int layer = uReflectionMode == 2 && (uReflectionReadyMask & (1 << probe)) != 0 ? probe : 0;
    return textureLod(uEnvironment, vec4(direction, float(layer)), lod).rgb;
}

// Reflections can be disabled without removing diffuse skylight or transmission fallback.
vec3 reflectedEnvironment(vec3 direction, float lod, int probe){
    return uReflectionMode == 0 ? vec3(0) : environmentRadiance(direction, lod, probe);
}

// Diffuse irradiance divided by pi, projected to nine spherical-harmonic coefficients.
vec3 diffuseEnvironment(vec3 n){
    float basis[9] = float[9](0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x,
        1.092548 * n.x * n.y, 1.092548 * n.y * n.z,
        0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
        0.546274 * (n.x * n.x - n.y * n.y));
    vec3 irradiance = vec3(0);
    for (int i = 0; i < 9; ++i){ irradiance += uIrradiance[i] * basis[i]; }
    return max(irradiance, vec3(0));
}

// Split-sum specular IBL with a matching GGX prefilter and integrated BRDF lookup.
vec3 environmentLighting(Material material, vec3 albedo, vec3 normal, vec3 view, float ao, int probe){
    float nv = max(dot(normal, view), 0.001);
    float roughness = clamp(material.roughness, 0.045, 1.0);
    if (uBrdfMode != 2){
        float glossRoughness = pow(2.0 / (material.shininess + 2.0), 0.25);
        vec3 reflected = reflectedEnvironment(reflect(-view, normal),
                                               glossRoughness * uEnvironmentMaxLod, probe);
        return ((1.0 - material.metallic) * albedo * diffuseEnvironment(normal) +
                 material.specular * reflected) * ao;
    }
    vec3 f0 = mix(vec3(0.04), clamp(albedo, 0.0, 1.0), material.metallic);
    vec3 fresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - nv, 5.0);
    vec3 diffuse = (1.0 - fresnel) * (1.0 - material.metallic) * albedo * diffuseEnvironment(normal);
    vec3 reflection = reflect(-view, normal);
    vec3 radiance = reflectedEnvironment(reflection, roughness * uEnvironmentMaxLod, probe);
    vec2 integrated = texture(uEnvironmentBrdf, vec2(nv, roughness)).rg;
    return (diffuse + radiance * (f0 * integrated.x + integrated.y)) * ao;
}
