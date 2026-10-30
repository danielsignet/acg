#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uPosition, uNormal, uRawAO, uAmbient;
uniform int uPass;

// Filters AO along each axis, then upsamples it without crossing surface discontinuities.
void main(){
    vec4 position = texture(uPosition, texCoord);
    bool resolve = uPass == 2;
    vec3 ambient = resolve ? texture(uAmbient, texCoord).rgb : vec3(1.0);
    if (position.w == 0.0){
        fragmentColor = vec4(ambient, 1.0);
        return;
    }
    vec3 normal = normalize(texture(uNormal, texCoord).xyz);
    vec2 size = vec2(textureSize(uRawAO, 0));
    vec2 center = floor(texCoord * size) + 0.5;
    float total = 0.0, weightSum = 0.0;
    int radiusX = resolve ? 1 : uPass == 0 ? 2 : 0;
    int radiusY = resolve ? 1 : uPass == 1 ? 2 : 0;
    for (int y = -radiusY; y <= radiusY; ++y){
        for (int x = -radiusX; x <= radiusX; ++x){
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
            float alignment = max(dot(normal, neighborNormal), 0.0);
            alignment *= alignment;
            alignment *= alignment;
            float weight = exp2((-0.5 * dot(offset, offset) - separation / 0.08) * 1.44269504) *
                           alignment * alignment;
            total += texture(uRawAO, uv).r * weight;
            weightSum += weight;
        }
    }
    float visibility = weightSum > 0.0001 ? total / weightSum : 1.0;
    // Shape the filtered visibility to keep contact shadows clear at the scene's scale.
    visibility = clamp(visibility, 0.0, 1.0);
    fragmentColor = resolve ? vec4(ambient, visibility * visibility * sqrt(visibility)) : vec4(vec3(visibility), 1.0);
}
