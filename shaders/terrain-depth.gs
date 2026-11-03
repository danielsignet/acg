#version 410 core
layout(triangles) in;
layout(triangle_strip,max_vertices=3) out;
in vec2 terrainUV[];
out vec3 shadowWorld;
out vec2 shadowUV;
uniform mat4 uLightVP;
#include "terrain.glsl"
void main(){
    for(int i=0;i<3;++i){
        vec4 p=uModel*vec4(terrainPoint(terrainUV[i]),1);
        shadowWorld=p.xyz;shadowUV=terrainUV[i]*uTerrainDimensions.xz*.5;
        gl_Position=uLightVP*p;EmitVertex();
    }
    EndPrimitive();
}
