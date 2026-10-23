#version 410 core
in vec2 uv;
uniform sampler2D uAtlas;
uniform vec4 uColor;
uniform bool uSolid;
out vec4 fragmentColor;

// Combines a glyph sample or solid fill with the overlay color.
void main(){
    float coverage = uSolid ? 1.0 : texture(uAtlas, uv).r;
    fragmentColor = vec4(uColor.rgb, uColor.a * coverage);
}
