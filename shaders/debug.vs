#version 410 core
layout(location = 0) in vec3 position;
uniform mat4 uVP;

void main(){
    gl_Position = uVP * vec4(position, 1.0);
}
