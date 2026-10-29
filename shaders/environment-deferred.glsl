in vec2 texCoord;
out vec4 fragmentColor;
#include "brdf.glsl"
#include "environment.glsl"
uniform vec3 uEye;
uniform mat4 uInverseProjection, uInverseView;
uniform samplerCubeArray uSky;
#ifdef MULTISAMPLE
uniform sampler2DMS uPosition, uNormal, uAlbedo, uSpecular, uEmissive, uAmbient;
vec4 surfaceValue(sampler2DMS source){ return texelFetch(source, ivec2(gl_FragCoord.xy), gl_SampleID); }
#else
uniform sampler2D uPosition, uNormal, uAlbedo, uSpecular, uEmissive, uAmbient;
vec4 surfaceValue(sampler2D source){ return texture(source, texCoord); }
#endif

// Adds HDR environment lighting per surface sample, or the sky for uncovered samples.
void main(){
    vec4 position = surfaceValue(uPosition);
    if (position.w == 0.0){
        vec4 view = uInverseProjection * vec4(texCoord * 2.0 - 1.0, 1, 1);
        vec3 direction=mat3(uInverseView)*view.xyz;
        fragmentColor = vec4(texture(uSky, vec4(direction,float(skyLayerForRay(uEye,direction)))).rgb, 0);
        return;
    }
    int flags = int(round(surfaceValue(uEmissive).a));
    if ((flags & 1) == 0 || (flags & 16) == 0 || (flags & 8) != 0){ discard; }
    vec4 normal = surfaceValue(uNormal), albedo = surfaceValue(uAlbedo);
    vec4 specular = surfaceValue(uSpecular), ambient = surfaceValue(uAmbient);
    Material material = Material(ambient.rgb, albedo.rgb, specular.rgb, specular.a, normal.a, albedo.a);
    fragmentColor = vec4(environmentLighting(material, albedo.rgb, normalize(normal.xyz),
                                              normalize(uEye - position.xyz), ambient.a, flags >> 5, position.xyz), 0);
}
