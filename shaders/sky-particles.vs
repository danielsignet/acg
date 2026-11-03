#version 410 core
layout(location=0) in vec4 positionSize;
layout(location=1) in vec4 colorPhase;
uniform mat4 uSkyProjection, uFormation[8];
uniform mat3 uSkyRotation;
uniform float uParticleTime, uParticleScale, uMaxPointSize;
out vec3 particleColor;
void main(){
    int group=int(colorPhase.w/8.0);
    float phase=colorPhase.w-float(group)*8.0;
    vec3 direction=normalize((uFormation[group]*vec4(positionSize.xyz,1)).xyz);
    float meteor=1.0;
    if(group==7){
        float seed=positionSize.x,tail=positionSize.y;
        float age=fract(uParticleTime*(900.0/4096.0)+seed);
        vec3 origin=normalize(vec3(-.68+seed*.24,.48+positionSize.z*.25,-.62+positionSize.z*.12));
        vec3 tangent=vec3(seed>.5 ? .8 : -.8,-.22-positionSize.z*.2,.12);
        tangent=normalize(tangent-origin*dot(origin,tangent));
        direction=normalize(origin+tangent*(age*4.0-.7-tail*.045));
        meteor=smoothstep(0.0,.025,age)*(1.0-smoothstep(.30,.38,age))*exp2(-tail*5.0);
    }
    vec4 clip=uSkyProjection*vec4(uSkyRotation*direction,0);
    clip.z=clip.w; // Infinite sky: preserve foreground depth and camera translation invariance.
    gl_Position=clip;
    float size=positionSize.w*uParticleScale;
    gl_PointSize=clamp(size,1.0,uMaxPointSize);
    float energy=min(1.0,size*size);
    float twinkle=group==0 ? .94+.06*sin(uParticleTime*(6.28318530718/4096.0)*(512.0+floor(phase*128.0))+phase) : 1.0;
    particleColor=colorPhase.rgb*(energy*twinkle*meteor);
}
