#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uPosition, uNormal;
uniform mat4 uView, uProjection;
uniform vec3 uSamples[64];
uniform int uSampleCount = 64;

// Samples a normal-oriented hemisphere in view space using world-space surface buffers.
void main(){
    vec4 world = texture(uPosition, texCoord);
    if (world.w == 0.0){
        fragmentColor = vec4(1.0);
        return;
    }
    vec3 position = (uView * vec4(world.xyz, 1.0)).xyz;
    vec3 normal = normalize(mat3(uView) * texture(uNormal, texCoord).xyz);
    vec2 tile = mod(floor(gl_FragCoord.xy), 2.0);
    float angle = 6.2831853 * fract(sin(dot(tile, vec2(12.9898, 78.233))) * 43758.5453);
    vec3 randomAxis = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = randomAxis - normal * dot(randomAxis, normal);
    if (dot(tangent, tangent) < 0.0001){
        tangent = cross(normal, abs(normal.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0));
    }
    tangent = normalize(tangent);
    mat3 basis = mat3(tangent, cross(normal, tangent), normal);
    const float radius = 1.5, bias = 0.025;
    float blocked = 0.0;
    for (int i = 0; i < 64; ++i){
        if(i>=uSampleCount) break;
        // Keep the complete radius/elevation distribution with fewer samples.
        vec3 samplePosition = position + basis * uSamples[i*64/uSampleCount] * radius;
        vec4 clip = uProjection * vec4(samplePosition, 1.0);
        if (clip.w <= 0.0){
            continue;
        }
        vec3 ndc = clip.xyz / clip.w;
        if (any(greaterThan(abs(ndc), vec3(1.0)))){
            continue;
        }
        vec4 observed = texture(uPosition, ndc.xy * 0.5 + 0.5);
        if (observed.w == 0.0){
            continue;
        }
        vec3 observedPosition = (uView * vec4(observed.xyz, 1.0)).xyz;
        // Reject the receiver's own plane despite nearest-texel depth quantization.
        if (dot(normal, observedPosition - position) <= bias){
            continue;
        }
        float depth = observedPosition.z;
        float separation = length(position - observedPosition);
        float range = 1.0 - smoothstep(radius * 0.5, radius, separation);
        blocked += depth >= samplePosition.z + bias ? range : 0.0;
    }
    fragmentColor = vec4(vec3(clamp(1.0 - blocked / float(uSampleCount), 0.0, 1.0)), 1.0);
}
