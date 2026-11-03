#version 410 core
layout(triangles) in;
layout(triangle_strip,max_vertices=3) out;
in vec2 terrainUV[];
out vec3 worldPosition,worldNormal;
out vec4 worldTangent;
out vec2 texCoord;
out vec3 furPosition,furNormal;
out vec4 furRootClip;
flat out float furLayer;
uniform mat4 uView,uProjection;
uniform mat3 uNormal;
#include "terrain.glsl"
void main(){
    for(int i=0;i<3;++i){
        vec2 uv=terrainUV[i];vec3 point=terrainPoint(uv),normal=terrainNormal(uv);
        vec4 p=uModel*vec4(point,1);
        worldPosition=p.xyz;worldNormal=normalize(uNormal*normal);
        worldTangent=vec4(0);texCoord=uv*uTerrainDimensions.xz*.5;
        furPosition=point;furNormal=normal;furLayer=0;
        furRootClip=uProjection*uView*p;
        gl_Position=furRootClip;EmitVertex();
    }
    EndPrimitive();
}
