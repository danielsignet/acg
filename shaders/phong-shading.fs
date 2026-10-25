#version 410 core
in vec3 worldPosition, worldNormal;
in vec2 texCoord;
out vec4 fragmentColor;

struct Material{
    vec3 ambient, diffuse, specular;
    float shininess;
};

struct Light{
    vec4 position;
    vec3 ambient, diffuse, specular, attenuation;
    bool enabled;
};

uniform Material uMaterial;
uniform Light uLights[8];
uniform int uLightCount;
uniform vec3 uEye, uTint, uEmissive;
uniform bool uLighting, uToon, uMultiTexture, uFog;
uniform sampler2D uTextures[4];
uniform bool uHasTexture[4];
uniform float uTextureMix, uAlphaCutoff, uOpacity;

// Shades the surface and discards fragments below the alpha threshold.
void main(){
    vec4 surface = vec4(uMaterial.diffuse * uTint, 1.0);
    if (uHasTexture[0]){
        surface *= texture(uTextures[0], texCoord);
    }
    if (surface.a < uAlphaCutoff){
        discard;
    }
    if (uMultiTexture){
        for (int i = 1; i < 4; ++i){
            if (uHasTexture[i]){
                surface.rgb *= mix(vec3(1.0), texture(uTextures[i], texCoord).rgb, uTextureMix);
            }
        }
    }
    vec3 n = normalize(worldNormal);
    if (!gl_FrontFacing){
        n = -n;
    }
    vec3 viewDirection = normalize(uEye - worldPosition);
    vec3 result = surface.rgb;
    if (uLighting){
        result = uMaterial.ambient * surface.rgb * 0.6;
        for (int i = 0; i < uLightCount; ++i){
            if (!uLights[i].enabled){
                continue;
            }
            vec3 lightDirection;
            float attenuation = 1.0;
            if (uLights[i].position.w == 0.0){
                lightDirection = normalize(uLights[i].position.xyz);
            } else{
                vec3 delta = uLights[i].position.xyz - worldPosition;
                float d = length(delta);
                lightDirection = delta / max(d, 0.0001);
                attenuation = 1.0 / max(dot(uLights[i].attenuation, vec3(1.0, d, d * d)), 0.0001);
            }
            float diffuse = max(dot(n, lightDirection), 0.0);
            float specular = 0.0;
            if (diffuse > 0.0){
                specular = pow(max(dot(viewDirection, reflect(-lightDirection, n)), 0.0),
                               uMaterial.shininess);
            }
            if (uToon){
                diffuse = floor(diffuse * 4.0) / 4.0;
                specular = step(0.45, specular);
            }
            result +=
                attenuation * (surface.rgb * (uLights[i].ambient + uLights[i].diffuse * diffuse) +
                               uMaterial.specular * uLights[i].specular * specular);
        }
        if (uToon){
            result *= smoothstep(0.05, 0.3, abs(dot(n, viewDirection)));
        }
    }
    result += uEmissive;
    vec3 mapped = pow(result / (result + vec3(0.65)), vec3(1.0 / 2.2));
    float fog = uFog ? 1.0 - exp(-0.00055 * dot(uEye - worldPosition, uEye - worldPosition)) : 0.0;
    fragmentColor =
        vec4(mix(mapped, vec3(0.035, 0.052, 0.078), clamp(fog, 0.0, 0.82)), surface.a * uOpacity);
}
