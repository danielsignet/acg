#version 410 core
in vec3 worldPosition, worldNormal;
in vec2 texCoord;
out vec4 fragmentColor;
#include "lighting.glsl"
#include "surface.glsl"
uniform bool uLinearOutput;

// Shades forward surfaces, including transparent geometry over deferred lighting.
void main(){
    vec4 surface = sampleSurface(texCoord);
    vec3 normal, plane;
    surfaceNormals(normal, plane);
    vec3 radiance = shadeSurface(worldPosition, normal, plane, surface, uMaterial, uEmissive,
                                 uLighting, uToon, uReceiveShadows, uLightMask);
    fragmentColor = vec4(uLinearOutput ? radiance : displaySurface(radiance, worldPosition, uFog),
                         surface.a * uOpacity);
}
