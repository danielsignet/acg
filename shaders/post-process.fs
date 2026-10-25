#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uColor, uBlur, uBloom, uDepth, uPosition, uEmissive;
uniform mat4 uInverseProjection;
uniform vec3 uEye;
uniform int uPass;
uniform bool uBloomEnabled, uDofEnabled;

// Converts a sampled depth value to a camera-space distance for any projection.
float viewDepth(vec2 uv){
    vec4 position =
        uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r * 2.0 - 1.0, 1.0);
    return abs(position.z / position.w);
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
        float focus = viewDepth(vec2(0.5));
        float distance = viewDepth(texCoord);
        float coc =
            smoothstep(max(0.75, focus * 0.08), max(3.0, focus * 0.5), abs(distance - focus));
        color = mix(color, texture(uBlur, texCoord).rgb, coc);
    }
    if (uBloomEnabled){
        color += texture(uBloom, texCoord).rgb * 0.25;
    }
    color = pow(max(color, vec3(0)) / (max(color, vec3(0)) + vec3(0.65)), vec3(1.0 / 2.2));
    vec4 position = texture(uPosition, texCoord);
    int flags = int(round(texture(uEmissive, texCoord).a));
    if (position.w > 0.0 && (flags & 2) != 0){
        float fog = 1.0 - exp(-0.00055 * dot(uEye - position.xyz, uEye - position.xyz));
        color = mix(color, vec3(0.035, 0.052, 0.078), clamp(fog, 0.0, 0.82));
    }
    fragmentColor = vec4(color, 1.0);
}
