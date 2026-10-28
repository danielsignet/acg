#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uColor, uDofFar, uDofNear, uBloom, uPosition, uEmissive;
uniform vec3 uEye;
uniform int uPass;
uniform bool uBloomEnabled, uDofEnabled;
#include "dof.glsl"
#include "display.glsl"

// Rejects half-resolution foreground colors when reconstructing a blurred background.
vec3 farColor(float radius, vec3 fallback){
    vec2 size = vec2(textureSize(uDofFar, 0));
    vec2 pixel = texCoord * size - 0.5;
    ivec2 base = ivec2(floor(pixel));
    vec2 fraction = fract(pixel);
    vec3 color = vec3(0);
    float total = 0.0;
    for (int i = 0; i < 4; ++i){
        ivec2 offset = ivec2(i % 2, i / 2);
        ivec2 samplePixel = clamp(base + offset, ivec2(0), ivec2(size) - 1);
        vec4 sampleColor = texelFetch(uDofFar, samplePixel, 0);
        vec2 weight = mix(vec2(1) - fraction, fraction, vec2(offset));
        float bilateral = clamp(1.0 - abs(sampleColor.a - radius) / max(1.0, radius * 0.25),
                                0.0, 1.0);
        float combined = weight.x * weight.y * bilateral;
        color += sampleColor.rgb * combined;
        total += combined;
    }
    return total > 0.0001 ? color / total : fallback;
}

// Extracts highlights, filters an intermediate target, or composes the enabled effects.
void main(){
    vec3 color = texture(uColor, texCoord).rgb;
    if (uPass == 1){
        float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
        fragmentColor = vec4(color * smoothstep(0.8, 1.8, brightness), 1.0);
        return;
    }
    if (uPass == 2 || uPass == 3){
        vec2 stepSize = 1.0 / vec2(textureSize(uColor, 0));
        vec2 direction = uPass == 2 ? vec2(stepSize.x, 0) : vec2(0, stepSize.y);
        vec3 result = color * 0.227027;
        const float weights[4] = float[](0.1945946, 0.1216216, 0.054054, 0.016216);
        for (int i = 1; i <= 4; ++i){
            result += (texture(uColor, texCoord + direction * float(i)).rgb +
                       texture(uColor, texCoord - direction * float(i)).rgb) *
                      weights[i - 1];
        }
        fragmentColor = vec4(result, 1.0);
        return;
    }
    if (uDofEnabled){
        float radius = dofRadius(viewDepth(texCoord));
        color = mix(color, farColor(radius, color), smoothstep(0.5, 2.0, radius));
        vec4 near = texture(uDofNear, texCoord);
        color = mix(color, near.rgb / max(near.a, 0.0001), clamp(near.a, 0.0, 1.0));
    }
    if (uBloomEnabled){
        color += texture(uBloom, texCoord).rgb * 0.25;
    }
    color = displayColor(color);
    vec4 position = texture(uPosition, texCoord);
    int flags = int(round(texture(uEmissive, texCoord).a));
    if (position.w > 0.0 && (flags & 2) != 0){
        float fog = 1.0 - exp(-0.00055 * dot(uEye - position.xyz, uEye - position.xyz));
        color = mix(color, vec3(0.035, 0.052, 0.078), clamp(fog, 0.0, 0.82));
    }
    fragmentColor = vec4(color, 1.0);
}
