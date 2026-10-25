#version 410 core
in vec3 shadowWorld;
in vec2 shadowUV;
uniform sampler2D uAlbedo;
uniform bool uHasAlbedo, uPointPass;
uniform float uAlphaCutoff, uShadowFarPlane;
uniform vec3 uShadowLightPosition;

void main(){
    if (uHasAlbedo && texture(uAlbedo, shadowUV).a < uAlphaCutoff){
        discard;
    }
    gl_FragDepth =
        uPointPass ? length(shadowWorld - uShadowLightPosition) / uShadowFarPlane : gl_FragCoord.z;
}
