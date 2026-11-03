#version 410 core
in vec3 particleColor;
out vec4 fragmentColor;
uniform vec3 uEye;
uniform mat4 uInverseProjection,uInverseView;
uniform vec4 uParticleViewport;
uniform int uParticleLayer;
#include "room-skies.glsl"
void main(){
    vec2 p=gl_PointCoord*2.0-1.0;
    float radius=dot(p,p);
    if(radius>1.0)discard;
    vec2 uv=(gl_FragCoord.xy-uParticleViewport.xy)/uParticleViewport.zw;
    vec3 ray=mat3(uInverseView)*(uInverseProjection*vec4(uv*2.0-1.0,1,1)).xyz;
    if(skyLayerForRay(uEye,ray)!=uParticleLayer)discard;
    fragmentColor=vec4(particleColor*exp2(-5.5*radius),0);
}
