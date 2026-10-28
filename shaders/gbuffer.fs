#version 410 core
in vec3 worldPosition, worldNormal;
in vec4 worldTangent;
in vec2 texCoord;
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gSpecular;
layout(location = 4) out vec4 gAmbient;
layout(location = 5) out vec4 gEmissive;
layout(location = 6) out vec4 gPlane;

#include "brdf.glsl"

#include "surface.glsl"
uniform bool uReceiveShadows, uEnvironmentMaterial, uMirror, uMirrorAvailable;
uniform vec2 uViewport;
uniform vec2 uViewportOrigin;

// Stores visible surface data without evaluating any lights.
void main(){
    vec4 surface = sampleSurface(texCoord);
    vec3 normal, plane;
    surfaceNormals(normal, plane);
    int flags = (uLighting ? 1 : 0) | (uFog ? 2 : 0) | (uReceiveShadows ? 4 : 0) |
                (uToon ? 8 : 0) | (uEnvironmentMaterial ? 16 : 0) | (uReflectionProbe << 5);
    gPosition = vec4(worldPosition, 1.0);
    gNormal = vec4(normal, uMaterial.roughness);
    gAlbedo = vec4(surface.rgb, uMaterial.metallic);
    gSpecular = vec4(uMaterial.specular, uMaterial.shininess);
    gAmbient = vec4(uMaterial.ambient, 1.0);
    gEmissive = vec4(surfaceEmission(), float(flags));
    gPlane = vec4(plane, float(uLightMask));
    if (uMirror){
        gAlbedo.rgb = vec3(0);
        gAmbient.rgb = vec3(0);
        gEmissive = vec4(uMirrorAvailable
            ? texture(uTextures[3], (gl_FragCoord.xy - uViewportOrigin) / uViewport).rgb : vec3(0.01), 0);
    }
}
