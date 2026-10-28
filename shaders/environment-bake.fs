#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
#include "brdf.glsl"
uniform sampler2D uEquirectangular;
uniform samplerCube uSourceCube;
uniform int uPass, uFace, uSampleCount;
uniform float uRoughness;

// OpenGL's cube-face orientation, including each face's vertical convention.
vec3 cubeDirection(vec2 uv){
    vec2 p = uv * 2.0 - 1.0;
    if (uFace == 0){ return normalize(vec3(1, -p.y, -p.x)); }
    if (uFace == 1){ return normalize(vec3(-1, -p.y, p.x)); }
    if (uFace == 2){ return normalize(vec3(p.x, 1, p.y)); }
    if (uFace == 3){ return normalize(vec3(p.x, -1, -p.y)); }
    if (uFace == 4){ return normalize(vec3(p.x, -p.y, 1)); }
    return normalize(vec3(-p.x, -p.y, -1));
}

// Deterministic Hammersley points avoid baking noise between reloads.
vec2 hammersley(uint index, uint count){
    return vec2(float(index) / float(count), float(bitfieldReverse(index)) * 2.3283064365386963e-10);
}

// Importance-samples GGX microfacet normals around the supplied surface normal.
vec3 sampleGGX(vec2 samplePoint, float roughness, vec3 normal){
    float alpha = roughness * roughness;
    float phi = 2.0 * PI * samplePoint.x;
    float cosine = sqrt((1.0 - samplePoint.y) / (1.0 + (alpha * alpha - 1.0) * samplePoint.y));
    float sine = sqrt(max(0.0, 1.0 - cosine * cosine));
    vec3 h = vec3(cos(phi) * sine, sin(phi) * sine, cosine);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, normal));
    return normalize(tangent * h.x + cross(normal, tangent) * h.y + normal * h.z);
}

// Converts the HDRI, prefilters rough reflections, or integrates the split-sum BRDF.
void main(){
    uint count = uint(max(uSampleCount, 1));
    if (uPass == 0){
        vec3 direction = cubeDirection(texCoord);
        vec2 uv = vec2(atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
                       asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
        fragmentColor = vec4(texture(uEquirectangular, uv).rgb, 1);
    } else if (uPass == 1){
        vec3 n = cubeDirection(texCoord);
        if (uRoughness < 0.001){ fragmentColor = vec4(texture(uSourceCube, n).rgb, 1); return; }
        vec3 color = vec3(0);
        float weight = 0.0;
        for (uint i = 0u; i < count; ++i){
            vec3 h = sampleGGX(hammersley(i, count), uRoughness, n);
            vec3 l = normalize(2.0 * dot(n, h) * h - n);
            float nl = max(dot(n, l), 0.0);
            if (nl > 0.0){
                float nh = max(dot(n, h), 0.001);
                float pdf = distributionGGX(nh, max(uRoughness * uRoughness, 0.0025)) * 0.25;
                float size = float(textureSize(uSourceCube, 0).x);
                float texelArea = 4.0 * PI / (6.0 * size * size);
                float sampleArea = 1.0 / (float(count) * max(pdf, 0.0001));
                float lod = max(0.0, 0.5 * log2(sampleArea / texelArea));
                color += textureLod(uSourceCube, l, lod).rgb * nl;
                weight += nl;
            }
        }
        fragmentColor = vec4(color / max(weight, 0.001), 1);
    } else{
        float nv = max(texCoord.x, 0.001), roughness = max(texCoord.y, 0.045);
        vec3 n = vec3(0, 0, 1), v = vec3(sqrt(1.0 - nv * nv), 0, nv);
        vec2 integral = vec2(0);
        float alpha = roughness * roughness;
        for (uint i = 0u; i < count; ++i){
            vec3 h = sampleGGX(hammersley(i, count), roughness, n);
            vec3 l = normalize(2.0 * dot(v, h) * h - v);
            float nl = max(l.z, 0.0), nh = max(h.z, 0.001), vh = max(dot(v, h), 0.0);
            if (nl > 0.0){
                float visibility = 4.0 * visibilityGGX(nv, nl, alpha) * nl * vh / nh;
                float fresnel = pow(1.0 - vh, 5.0);
                integral += vec2(1.0 - fresnel, fresnel) * visibility;
            }
        }
        fragmentColor = vec4(integral / float(count), 0, 1);
    }
}
