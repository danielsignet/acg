in vec2 texCoord;
out vec4 fragmentColor;
#include "lighting.glsl"
#ifdef MULTISAMPLE
uniform sampler2DMS uGPosition, uGNormal, uGAlbedo, uGSpecular, uGAmbient, uGEmissive, uGPlane;
// gl_SampleID forces separate lighting for each coverage sample, before color resolution.
vec4 readSurface(sampler2DMS source, vec2 uv){
    return texelFetch(source, ivec2(gl_FragCoord.xy), gl_SampleID);
}
#else
uniform sampler2D uGPosition, uGNormal, uGAlbedo, uGSpecular, uGAmbient, uGEmissive, uGPlane;
vec4 readSurface(sampler2D source, vec2 uv){ return texture(source, uv); }
#endif
// Evaluates lighting once for the nearest opaque fragment at each screen pixel.
void main(){
    vec4 position = readSurface(uGPosition, texCoord);
    if (position.w == 0.0){
        // Inverse filmic values preserve the forward renderer's display-space clear color.
        vec3 background = vec3(0.00245153, 0.00502452, 0.00977764);
        fragmentColor = vec4(uEnvironmentEnabled ? vec3(0) : background, 1.0);
        return;
    }
    vec4 specular = readSurface(uGSpecular, texCoord);
    vec4 emissive = readSurface(uGEmissive, texCoord);
    vec4 plane = readSurface(uGPlane, texCoord);
    vec4 albedo = readSurface(uGAlbedo, texCoord);
    int flags = int(round(emissive.a));
    Material material = Material(readSurface(uGAmbient, texCoord).rgb, albedo.rgb,
                                 specular.rgb, specular.a,
                                 readSurface(uGNormal, texCoord).a, albedo.a);
    vec3 radiance =
        shadeSurface(position.xyz, normalize(readSurface(uGNormal, texCoord).xyz), normalize(plane.xyz),
                     vec4(material.diffuse, 1.0), material, emissive.rgb, (flags & 1) != 0,
                     (flags & 8) != 0, (flags & 4) != 0, int(round(plane.a)),
                     readSurface(uGAmbient, texCoord).a, (flags & 16) != 0);
    fragmentColor = vec4(radiance, 1.0);
}
