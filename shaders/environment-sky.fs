#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform samplerCube uSky;
uniform mat4 uInverseProjection, uInverseView;
uniform bool uLinearOutput;
#include "display.glsl"

// A view direction ignores camera translation, so the sky remains at infinity.
void main(){
    vec4 view = uInverseProjection * vec4(texCoord * 2.0 - 1.0, 1, 1);
    vec3 direction = mat3(uInverseView) * view.xyz;
    vec3 color = texture(uSky, direction).rgb;
    if (!uLinearOutput){ color = displayColor(color); }
    fragmentColor = vec4(color, 1);
}
