#version 410 core
in vec2 texCoord;out vec4 fragmentColor;uniform sampler2D uParticleImage;
#include "display.glsl"
void main(){fragmentColor=vec4(displayColor(texture(uParticleImage,texCoord).rgb),1);}
