#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uColor;
uniform int uPass;
#include "dof.glsl"

const int SampleCount = 48;

// A stable equal-area disk avoids directional streaks from a separable blur.
vec2 diskSample(int index){
    float angle = float(index) * 2.39996323;
    return sqrt((float(index) + 0.5) / float(SampleCount)) * vec2(cos(angle), sin(angle));
}

// Prevents focused or foreground colors contaminating the downsampled background.
vec4 prefilter(){
    ivec2 size = textureSize(uColor, 0);
    ivec2 base = ivec2(gl_FragCoord.xy) * 2;
    vec3 colors[4];
    float radii[4];
    float nearest = maxDofRadius();
    for (int i = 0; i < 4; ++i){
        ivec2 pixel = min(base + ivec2(i % 2, i / 2), size - 1);
        vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
        colors[i] = texelFetch(uColor, pixel, 0).rgb;
        radii[i] = dofRadius(viewDepth(uv));
        nearest = min(nearest, radii[i]);
    }
    vec3 color = vec3(0);
    float total = 0.0;
    for (int i = 0; i < 4; ++i){
        float weight = clamp(1.0 - abs(radii[i] - nearest), 0.0, 1.0);
        color += colors[i] * weight;
        total += weight;
    }
    return vec4(color / total, nearest);
}

// Blurs the background without pulling a sharp nearer model into its silhouette.
vec4 farBlur(){
    vec4 center = texture(uColor, texCoord);
    float radius = max(0.0, center.a);
    if (radius < 0.5){
        return center;
    }
    vec3 color = center.rgb;
    float total = 1.0;
    for (int i = 0; i < SampleCount; ++i){
        vec2 uv = texCoord + diskSample(i) * radius / vec2(textureSize(uDepth, 0));
        vec4 sampleColor = texture(uColor, uv);
        float weight = smoothstep(0.0, 1.0, sampleColor.a) *
            clamp(1.0 - (radius - sampleColor.a) / max(radius, 1.0), 0.0, 1.0);
        color += sampleColor.rgb * weight;
        total += weight;
    }
    return vec4(color / total, center.a);
}

// Gathers each foreground sample over its own disk, including beyond its silhouette.
vec4 nearBlur(){
    vec4 result = vec4(0);
    float maximum = maxDofRadius();
    for (int i = 0; i < SampleCount; ++i){
        vec2 offset = diskSample(i) * maximum;
        vec4 sampleColor = texture(uColor, texCoord + offset / vec2(textureSize(uDepth, 0)));
        float radius = max(0.0, -sampleColor.a);
        float coverage = smoothstep(length(offset) - 0.75, length(offset) + 0.75, radius);
        float weight = coverage * smoothstep(0.5, 2.0, radius) * maximum * maximum /
            max(radius * radius, 1.0);
        result += vec4(sampleColor.rgb, 1.0) * weight;
    }
    return result / float(SampleCount);
}

// Prefilters once and produces separate background and premultiplied foreground layers.
void main(){
    fragmentColor = uPass == 0 ? prefilter() : uPass == 1 ? farBlur() : nearBlur();
}
