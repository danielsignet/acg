#version 410 core
layout(triangles) in;
layout(triangle_strip,max_vertices=3) out;
in vec2 terrainUV[];
out vec3 worldPosition,worldNormal;
out vec4 worldTangent;
out vec2 texCoord;
uniform mat4 uView,uProjection;
uniform mat3 uNormal;
#include "terrain.glsl"
void main(){
    for(int i=0;i<3;++i){
        vec2 uv=terrainUV[i];vec4 p=uModel*vec4(terrainPoint(uv),1);
        worldPosition=p.xyz;worldNormal=normalize(uNormal*terrainNormal(uv));
        worldTangent=vec4(0);texCoord=uv*uTerrainDimensions.xz*.5;
        gl_Position=uProjection*uView*p;EmitVertex();
    }
    EndPrimitive();
}
