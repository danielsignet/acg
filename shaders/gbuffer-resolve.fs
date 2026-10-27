#version 410 core
uniform sampler2DMS uGPosition, uGNormal, uGAlbedo, uGSpecular, uGAmbient, uGEmissive, uGPlane, uDepth;
layout(location = 0) out vec4 position;
layout(location = 1) out vec4 normal;
layout(location = 2) out vec4 albedo;
layout(location = 3) out vec4 specular;
layout(location = 4) out vec4 ambient;
layout(location = 5) out vec4 emissive;
layout(location = 6) out vec4 plane;

// Keep all diagnostic/AO surface attributes from one nearest sample, never average materials.
void main(){
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int nearest = 0;
    float depth = texelFetch(uDepth, pixel, 0).r;
    for (int sampleIndex = 1; sampleIndex < 4; ++sampleIndex){
        float candidate = texelFetch(uDepth, pixel, sampleIndex).r;
        if (candidate < depth){ depth = candidate; nearest = sampleIndex; }
    }
    position = texelFetch(uGPosition, pixel, nearest);
    normal = texelFetch(uGNormal, pixel, nearest);
    albedo = texelFetch(uGAlbedo, pixel, nearest);
    specular = texelFetch(uGSpecular, pixel, nearest);
    ambient = texelFetch(uGAmbient, pixel, nearest);
    emissive = texelFetch(uGEmissive, pixel, nearest);
    plane = texelFetch(uGPlane, pixel, nearest);
    gl_FragDepth = depth;
}
