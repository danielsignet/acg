const float PI = 3.141592653589793;
struct Material{
    vec3 ambient, diffuse, specular;
    float shininess, roughness, metallic;
};
uniform int uBrdfMode;

// Schlick Fresnel with colored normal-incidence reflectance for conductors.
vec3 fresnelSchlick(float cosine, vec3 f0){
    float f = pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
    return f0 + (1.0 - f0) * f;
}

// GGX normal distribution for the Cook-Torrance microfacet model.
float distributionGGX(float nh, float alpha){
    float a2 = alpha * alpha;
    float denominator = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 1e-8);
}

// Height-correlated Smith visibility, including the Cook-Torrance denominator.
float visibilityGGX(float nv, float nl, float alpha){
    float a2 = alpha * alpha;
    float v = nl * sqrt(nv * nv * (1.0 - a2) + a2);
    float l = nv * sqrt(nl * nl * (1.0 - a2) + a2);
    return 0.5 / max(v + l, 1e-6);
}

// Evaluates energy-sharing diffuse and specular terms before multiplication by N dot L.
void cookTorrance(Material material, vec3 albedo, vec3 n, vec3 v, vec3 l,
                  out vec3 diffuse, out vec3 specular){
    vec3 h = normalize(v + l);
    float nl = max(dot(n, l), 0.0), nv = max(dot(n, v), 0.0001);
    float alpha = max(material.roughness * material.roughness, 0.0025);
    vec3 f0 = mix(vec3(0.04), clamp(albedo, 0.0, 1.0), material.metallic);
    vec3 f = fresnelSchlick(max(dot(v, h), 0.0), f0);
    diffuse = (1.0 - f) * (1.0 - material.metallic) * albedo / PI;
    specular = distributionGGX(max(dot(n, h), 0.0), alpha) *
               visibilityGGX(nv, nl, alpha) * f;
}
