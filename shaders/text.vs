#version 410 core
layout(location = 0) in vec4 vertex;
uniform vec2 uScreen;
out vec2 uv;

// Maps overlay pixel coordinates into clip space.
void main(){
    uv = vertex.zw;
    gl_Position =
        vec4(vertex.x / uScreen.x * 2.0 - 1.0, 1.0 - vertex.y / uScreen.y * 2.0, 0.0, 1.0);
}
