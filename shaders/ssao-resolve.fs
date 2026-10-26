#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uPosition, uNormal, uRawAO, uAmbient;
uniform mat4 uView;

// Upsamples and smooths AO without blurring it across depth or normal discontinuities.
void main(){
    vec4 position = texture(uPosition, texCoord);
    vec3 ambient = texture(uAmbient, texCoord).rgb;
    if (position.w == 0.0){
        fragmentColor = vec4(ambient, 1.0);
        return;
    }
    float depth = (uView * vec4(position.xyz, 1.0)).z;
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
            float neighborDepth = (uView * vec4(neighbor.xyz, 1.0)).z;
            vec3 neighborNormal = normalize(texture(uNormal, uv).xyz);
            float spatial = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
            float weight = spatial * exp(-abs(neighborDepth - depth) / 0.18) *
                           pow(max(dot(normal, neighborNormal), 0.0), 8.0);
            total += texture(uRawAO, uv).r * weight;
            weightSum += weight;
        }
    }
    float visibility = weightSum > 0.0001 ? total / weightSum : 1.0;
    fragmentColor = vec4(ambient, clamp(visibility, 0.0, 1.0));
}
