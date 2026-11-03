#version 410 core
layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in mat4 instanceModel;
uniform mat4 uModel, uLightVP;
uniform bool uInstanced;
out vec3 shadowWorld;
out vec2 shadowUV;
#include "bounce.glsl"
#include "tree-deformation.glsl"

void main(){
    vec3 deformationScale;
    float deformationOffset;
    bouncePose(deformationScale,deformationOffset);
    vec4 world = (uInstanced ? instanceModel : uModel) *
        vec4(treePosition(position)*deformationScale+vec3(0,deformationOffset,0), 1.0);
    shadowWorld = world.xyz;
    shadowUV = texCoord;
    gl_Position = uLightVP * world;
}
