uniform bool uFurPass;
uniform int uFurLayers;
uniform float uFurDensity;
in vec3 furPosition, furNormal;
flat in float furLayer;

// Object-space triplanar roots also work on source meshes without UV coordinates.
// The same root texture is used in every shell; its value stores strand height.
float furCoverage(){
    vec3 p=furPosition*uFurDensity;
    vec3 weights=pow(abs(normalize(furNormal)),vec3(8));
    weights/=max(dot(weights,vec3(1)),1e-6);
    vec3 height=vec3(texture(uTextures[2],p.yz).r,texture(uTextures[2],p.xz).r,
                     texture(uTextures[2],p.xy).r);
    vec3 width=clamp(vec3(fwidth(height.x),fwidth(height.y),fwidth(height.z)),vec3(.015),vec3(.15));
    return dot(weights,smoothstep(vec3(furLayer)-width,vec3(furLayer)+width,height));
}
