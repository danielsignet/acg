#version 410 core
in vec3 worldPosition, worldNormal;
in vec4 worldTangent;
in vec2 texCoord;
out vec4 fragmentColor;
#include "lighting.glsl"
#include "surface.glsl"
#include "environment.glsl"
uniform bool uLinearOutput;
uniform bool uEnvironmentMaterial, uMirror, uMirrorAvailable, uClipEnabled;
uniform vec2 uViewport;
uniform vec2 uViewportOrigin;
uniform vec4 uClipPlane;
uniform bool uGlass;
uniform float uIor;
uniform vec4 uGlassSphere;
uniform mat4 uView, uProjection;
#include "glass.glsl"
#include "room-window.glsl"
#include "room-portal.glsl"

// Shades forward surfaces, including transparent geometry over deferred lighting.
void main(){
    if (outsideRoomWindow(worldPosition)) discard;
    if (uClipEnabled && dot(vec4(worldPosition, 1), uClipPlane) < 0.001){ discard; }
    if (uRoomPortal){
        vec3 color=roomPortalColor(worldPosition);
        fragmentColor=vec4(uLinearOutput ? color : displayColor(color),1);
        return;
    }
    vec4 surface = sampleSurface(texCoord);
    vec3 normal, plane;
    surfaceNormals(normal, plane);
    if (uGlass){
        fragmentColor = vec4(glassColor(surface.rgb, normal), surface.a * uOpacity);
        return;
    }
    vec3 radiance = shadeSurface(worldPosition, normal, plane, surface, uMaterial, surfaceEmission(),
                                 uLighting, uToon, uReceiveShadows, uLightMask, 1.0, uEnvironmentMaterial);
    if (uLighting && !uToon && uEnvironmentEnabled && uEnvironmentMaterial){
        radiance += environmentLighting(uMaterial, surface.rgb, normal,
                                         normalize(uEye - worldPosition), 1.0, uReflectionProbe,worldPosition);
    }
    if (uMirror){
        radiance = uMirrorAvailable ? texture(uTextures[3], (gl_FragCoord.xy - uViewportOrigin) / uViewport).rgb
                                    : vec3(0.01);
    }
    fragmentColor = vec4(uLinearOutput ? radiance : displaySurface(radiance, worldPosition, uFog),
                         surface.a * uOpacity);
}
