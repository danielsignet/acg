#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uColor, uDepth;
uniform bool uFxaa, uWriteDepth;

float luma(vec3 color){ return dot(color, vec3(0.299, 0.587, 0.114)); }

// FXAA edge search in display space, following NVIDIA's FXAA quality approach.
vec3 fxaa(vec2 uv){
    vec2 pixel = 1.0 / vec2(textureSize(uColor, 0));
    vec3 center = texture(uColor, uv).rgb;
    float m = luma(center);
    float n = luma(texture(uColor, uv + vec2(0, pixel.y)).rgb);
    float s = luma(texture(uColor, uv - vec2(0, pixel.y)).rgb);
    float e = luma(texture(uColor, uv + vec2(pixel.x, 0)).rgb);
    float w = luma(texture(uColor, uv - vec2(pixel.x, 0)).rgb);
    float low = min(m, min(min(n, s), min(e, w)));
    float high = max(m, max(max(n, s), max(e, w)));
    float range = high - low;
    if (range < max(0.0312, high * 0.125)){ return center; }
    float nw = luma(texture(uColor, uv + vec2(-pixel.x, pixel.y)).rgb);
    float ne = luma(texture(uColor, uv + pixel).rgb);
    float sw = luma(texture(uColor, uv - pixel).rgb);
    float se = luma(texture(uColor, uv + vec2(pixel.x, -pixel.y)).rgb);
    float horizontal = abs(nw + sw - 2.0 * w) + 2.0 * abs(n + s - 2.0 * m) +
                       abs(ne + se - 2.0 * e);
    float vertical = abs(nw + ne - 2.0 * n) + 2.0 * abs(w + e - 2.0 * m) +
                     abs(sw + se - 2.0 * s);
    bool horizontalEdge = horizontal >= vertical;
    float a = horizontalEdge ? s : w;
    float b = horizontalEdge ? n : e;
    bool negative = abs(a - m) >= abs(b - m);
    float gradient = max(abs(a - m), abs(b - m));
    float edgeLuma = 0.5 * (m + (negative ? a : b));
    vec2 normal = horizontalEdge ? vec2(0, pixel.y) : vec2(pixel.x, 0);
    if (negative){ normal = -normal; }
    vec2 along = horizontalEdge ? vec2(pixel.x, 0) : vec2(0, pixel.y);
    vec2 origin = uv + normal * 0.5;
    vec2 left = origin - along, right = origin + along;
    float leftDelta = luma(texture(uColor, left).rgb) - edgeLuma;
    float rightDelta = luma(texture(uColor, right).rgb) - edgeLuma;
    const float steps[10] = float[10](1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 4.0, 8.0);
    for (int i = 0; i < 10; ++i){
        bool leftDone = abs(leftDelta) >= gradient * 0.25;
        bool rightDone = abs(rightDelta) >= gradient * 0.25;
        if (leftDone && rightDone){ break; }
        if (!leftDone){ left -= along * steps[i]; leftDelta = luma(texture(uColor, left).rgb) - edgeLuma; }
        if (!rightDone){ right += along * steps[i]; rightDelta = luma(texture(uColor, right).rgb) - edgeLuma; }
    }
    float leftDistance = horizontalEdge ? uv.x - left.x : uv.y - left.y;
    float rightDistance = horizontalEdge ? right.x - uv.x : right.y - uv.y;
    bool nearerLeft = leftDistance < rightDistance;
    float delta = nearerLeft ? leftDelta : rightDelta;
    float edgeOffset = 0.5 - min(leftDistance, rightDistance) / (leftDistance + rightDistance);
    if ((delta < 0.0) == (m < edgeLuma)){ edgeOffset = 0.0; }
    float average = (2.0 * (n + s + e + w) + nw + ne + sw + se) / 12.0;
    float subpixel = clamp(abs(average - m) / range, 0.0, 1.0);
    subpixel = subpixel * subpixel * (3.0 - 2.0 * subpixel);
    subpixel = subpixel * subpixel * 0.75;
    return texture(uColor, uv + normal * max(edgeOffset, subpixel)).rgb;
}

// This pass runs before any text, selection boxes or crosshair is drawn.
void main(){
    gl_FragDepth = uWriteDepth ? texture(uDepth, texCoord).r : 1.0;
    fragmentColor = vec4(uFxaa ? fxaa(texCoord) : texture(uColor, texCoord).rgb, 1.0);
}
