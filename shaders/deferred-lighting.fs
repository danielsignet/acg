#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
#include "lighting.glsl"
uniform sampler2D uGPosition, uGNormal, uGAlbedo, uGSpecular, uGAmbient, uGEmissive, uGPlane;

// Evaluates lighting once for the nearest opaque fragment at each screen pixel.
void main(){
    vec4 position = texture(uGPosition, texCoord);
    if (position.w == 0.0){
        vec3 background = pow(vec3(0.035, 0.052, 0.078), vec3(2.2));
        fragmentColor = vec4(0.65 * background / (1.0 - background), 1.0);
        return;
    }
    vec4 specular = texture(uGSpecular, texCoord);
    vec4 emissive = texture(uGEmissive, texCoord);
    vec4 plane = texture(uGPlane, texCoord);
    int flags = int(round(emissive.a));
    Material material = Material(texture(uGAmbient, texCoord).rgb, texture(uGAlbedo, texCoord).rgb,
                                 specular.rgb, specular.a);
    vec3 radiance =
        shadeSurface(position.xyz, normalize(texture(uGNormal, texCoord).xyz), normalize(plane.xyz),
                     vec4(material.diffuse, 1.0), material, emissive.rgb, (flags & 1) != 0,
                     (flags & 8) != 0, (flags & 4) != 0, int(round(plane.a)),
                     texture(uGAmbient, texCoord).a);
    fragmentColor = vec4(radiance, 1.0);
}
