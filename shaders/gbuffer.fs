#version 410 core
in vec3 worldPosition, worldNormal;
in vec2 texCoord;
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gSpecular;
layout(location = 4) out vec4 gAmbient;
layout(location = 5) out vec4 gEmissive;
layout(location = 6) out vec4 gPlane;

struct Material{
    vec3 ambient, diffuse, specular;
    float shininess;
};

#include "surface.glsl"
uniform bool uReceiveShadows;

// Stores visible surface data without evaluating any lights.
void main(){
    vec4 surface = sampleSurface(texCoord);
    vec3 normal, plane;
    surfaceNormals(normal, plane);
    int flags = (uLighting ? 1 : 0) | (uFog ? 2 : 0) | (uReceiveShadows ? 4 : 0) | (uToon ? 8 : 0);
    gPosition = vec4(worldPosition, 1.0);
    gNormal = vec4(normal, 0.0);
    gAlbedo = vec4(surface.rgb, 1.0);
    gSpecular = vec4(uMaterial.specular, uMaterial.shininess);
    gAmbient = vec4(uMaterial.ambient, 1.0);
    gEmissive = vec4(uEmissive, float(flags));
    gPlane = vec4(plane, float(uLightMask));
}
