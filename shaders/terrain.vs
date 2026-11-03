#version 410 core
layout(location=2) in vec2 vertexTexCoord;
out vec2 terrainUV;
void main(){ terrainUV=vertexTexCoord;gl_Position=vec4(vertexTexCoord,0,1); }
