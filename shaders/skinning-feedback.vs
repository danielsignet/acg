#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 7) in vec4 tangent;
uniform usamplerBuffer uJoints;
uniform samplerBuffer uWeights, uPalette;
out vec3 skinnedPosition, skinnedNormal;
out vec2 skinnedUv;
out vec4 skinnedTangent;

// Skin each unique vertex once; every color, depth and reflection view reuses the output.
void main(){
    vec3 p = vec3(0), n = vec3(0), t = vec3(0);
    float total = 0.0;
    for (int slot = 0; slot < 12; ++slot){
        int texel = gl_VertexID * 3 + slot / 4;
        float weight = texelFetch(uWeights, texel)[slot % 4];
        if (weight <= 0.0){ break; }
        int joint = int(texelFetch(uJoints, texel)[slot % 4]) * 7;
        mat4 skin = mat4(texelFetch(uPalette, joint), texelFetch(uPalette, joint + 1),
                         texelFetch(uPalette, joint + 2), texelFetch(uPalette, joint + 3));
        mat3 basis = mat3(texelFetch(uPalette, joint + 4).xyz,
                          texelFetch(uPalette, joint + 5).xyz, texelFetch(uPalette, joint + 6).xyz);
        p += (skin * vec4(position, 1)).xyz * weight;
        n += basis * normal * weight;
        t += mat3(skin) * tangent.xyz * weight;
        total += weight;
    }
    skinnedPosition = total > 0.0 ? p / total : position;
    skinnedNormal = total > 0.0 && length(n) > 1e-8 ? normalize(n) : normal;
    t -= skinnedNormal * dot(skinnedNormal, t);
    skinnedTangent = total > 0.0
        ? vec4(length(t) > 1e-8 ? normalize(t) : vec3(0), tangent.w) : tangent;
    skinnedUv = uv;
    gl_Position = vec4(0, 0, 0, 1);
}
