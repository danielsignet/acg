#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uPosition, uNormal, uRawAO, uAmbient;

// Upsamples and smooths AO without blurring it across depth or normal discontinuities.
void main(){
    vec4 position = texture(uPosition, texCoord);
    vec3 ambient = texture(uAmbient, texCoord).rgb;
    if (position.w == 0.0){
        fragmentColor = vec4(ambient, 1.0);
        return;
    }
    vec3 normal = normalize(texture(uNormal, texCoord).xyz);
    vec2 size = vec2(textureSize(uRawAO, 0));
    vec2 center = floor(texCoord * size) + 0.5;
    float total = 0.0, weightSum = 0.0;
    for (int y = -1; y <= 1; ++y){
        for (int x = -1; x <= 1; ++x){
            vec2 uv = (center + vec2(x, y)) / size;
            if (any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1)))){
                continue;
            }
            vec4 neighbor = texture(uPosition, uv);
            if (neighbor.w == 0.0){
                continue;
            }
            vec3 neighborNormal = normalize(texture(uNormal, uv).xyz);
            vec2 offset = center + vec2(x, y) - texCoord * size;
            float separation = abs(dot(neighbor.xyz - position.xyz, normal));
            float weight = exp(-0.5 * dot(offset, offset) - separation / 0.08) *
                           pow(max(dot(normal, neighborNormal), 0.0), 8.0);
            total += texture(uRawAO, uv).r * weight;
            weightSum += weight;
        }
    }
    float visibility = weightSum > 0.0001 ? total / weightSum : 1.0;
    // Shape the filtered visibility to keep contact shadows clear at the scene's scale.
    fragmentColor = vec4(ambient, pow(clamp(visibility, 0.0, 1.0), 2.5));
}
