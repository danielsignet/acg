#version 410 core
out vec2 texCoord;

// Generates a screen-aligned quad without a vertex buffer.
void main(){
    texCoord = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    gl_Position = vec4(texCoord * 2.0 - 1.0, 0.0, 1.0);
}
