uniform bool uFurPass;
uniform int uFurLayers;
uniform float uFurDensity;
uniform bool uFurSoft;
uniform bool uFurCachedLighting;
in vec3 furPosition, furNormal;
in vec4 furRootClip;
flat in float furLayer;

// Object-space triplanar roots also work on source meshes without UV coordinates.
// The same root texture is used in every shell; its value stores strand height.
float furCoverage(){
    vec3 p=furPosition*uFurDensity;
    vec3 weights=pow(abs(normalize(furNormal)),vec3(8));
    weights/=max(dot(weights,vec3(1)),1e-6);
    // Averaging height in ordinary mipmaps cuts off long thin strands entirely.
    // Sample the root profile itself; derivative smoothing below filters coverage.
    vec3 height=vec3(textureLod(uTextures[2],p.yz,0).r,textureLod(uTextures[2],p.xz,0).r,
                     textureLod(uTextures[2],p.xy,0).r);
    vec3 width=clamp(vec3(fwidth(height.x),fwidth(height.y),fwidth(height.z)),vec3(.015),vec3(.15));
    return dot(weights,smoothstep(vec3(furLayer)-width,vec3(furLayer)+width,height));
}

vec3 furAlbedo(vec3 color){
    if(!uFurSoft || uFurLength<=0.0)return color;
    // Subtle groom variation follows the mesh rather than the world/camera.
    float mottling=.5+.5*sin(furPosition.x*5.0+sin(furPosition.y*3.0)+furPosition.z*4.0);
    color*=mix(.72,1.12,mottling);
    // Dark roots provide depth beneath the translucent, tapered shells.
    return color*(uFurPass?mix(.36,1.0,sqrt(furLayer)):.28);
}
