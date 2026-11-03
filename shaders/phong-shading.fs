#version 410 core
in vec3 worldPosition, worldNormal;
in vec4 worldTangent;
in vec2 texCoord;
out vec4 fragmentColor;
#include "lighting.glsl"
#include "surface.glsl"
#include "fur-fragment.glsl"
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
    vec2 coordinates = parallaxCoordinates(texCoord);
    if (outsideRoomWindow(worldPosition)) discard;
    if (uClipEnabled && dot(vec4(worldPosition, 1), uClipPlane) < 0.001){ discard; }
    if (uRoomPortal){
        vec3 color=roomPortalColor(worldPosition);
        fragmentColor=vec4(uLinearOutput ? color : displayColor(color),1);
        return;
    }
    vec4 surface = sampleSurface(coordinates);
    surface.rgb=furAlbedo(surface.rgb);
    float furAlpha=uFurPass?furCoverage():1.0;
    if(furAlpha<.015) discard;
    vec3 radiance;
    vec2 rootUv=furRootClip.xy/max(furRootClip.w,1e-6)*.5+.5;
    bool reuseRoot=uFurPass && uFurCachedLighting && furRootClip.w>0.0 &&
        all(greaterThanEqual(rootUv,vec2(0))) && all(lessThanEqual(rootUv,vec2(1)));
    if(reuseRoot){
        // Opaque root lighting already includes lights, shadows and ambient occlusion.
        // Recover the shell's root/tip colour without repeating PCF for every layer.
        radiance=texture(uTextures[3],rootUv).rgb*mix(.36,1.0,sqrt(furLayer))/.28;
    }else{
    vec3 normal, plane;
    surfaceNormals(coordinates, normal, plane);
    if (uGlass){
        fragmentColor = vec4(glassColor(surface.rgb, normal), surface.a * uOpacity);
        return;
    }
    radiance = shadeSurface(worldPosition, normal, plane, surface, uMaterial, surfaceEmission(),
                                 uLighting, uToon, uReceiveShadows, uLightMask, 1.0, uEnvironmentMaterial);
    if (uLighting && !uToon && uEnvironmentEnabled && uEnvironmentMaterial){
        radiance += environmentLighting(uMaterial, surface.rgb, normal,
                                         normalize(uEye - worldPosition), 1.0, uReflectionProbe,worldPosition);
    }
    if (uMirror){
        radiance = uMirrorAvailable ? texture(uTextures[3], (gl_FragCoord.xy - uViewportOrigin) / uViewport).rgb
                                    : vec3(0.01);
    }
    }
    // Additive shells use SRC_ALPHA + ONE. Normalize layer energy so changing the
    // shell count changes sampling quality rather than multiplying brightness.
    if(uFurPass) radiance*=3.0/float(max(uFurLayers,1))*mix(.22,1.0,furLayer);
    fragmentColor = vec4(uLinearOutput ? radiance : displaySurface(radiance, worldPosition, uFog),
                         surface.a * uOpacity * furAlpha);
}
