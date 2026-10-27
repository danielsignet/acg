#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uAmbient;

// Only alpha is writable: keep each sample's ambient material and replace its AO visibility.
void main(){ fragmentColor = vec4(0, 0, 0, texture(uAmbient, texCoord).a); }
