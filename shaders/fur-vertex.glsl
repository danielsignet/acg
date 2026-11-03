uniform bool uFurPass;
uniform float uFurLength;
uniform int uFurLayers;
uniform vec3 uFurBend;
out vec3 furPosition, furNormal;
flat out float furLayer;

vec3 furOffset(vec3 normal){
    furPosition=vertexPosition;
    furNormal=normal;
    furLayer=uFurPass?(float(gl_InstanceID)+.5)/float(max(uFurLayers,1)):0.0;
    return normal*(uFurLength*furLayer)+uFurBend*(furLayer*furLayer);
}
