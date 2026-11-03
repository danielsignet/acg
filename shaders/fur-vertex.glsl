uniform bool uFurPass;
uniform float uFurLength;
uniform int uFurLayers;
uniform vec3 uFurBend;
uniform vec3 uFurMotion;
uniform bool uFurSoft;
uniform bool uFurFace;
out vec3 furPosition, furNormal;
out vec4 furRootClip;
flat out float furLayer;

vec3 furOffset(vec3 normal){
    furPosition=vertexPosition;
    furNormal=normal;
    furLayer=uFurPass?(float(gl_InstanceID)+.5)/float(max(uFurLayers,1)):0.0;
    vec3 motion=uFurSoft?uFurMotion:vec3(0);
    // Keep roots attached; motion grows quadratically towards the free tips.
    motion-=normal*dot(motion,normal);
    float groom=1.0;
    if(uFurFace){
        float face=smoothstep(.30,.50,vertexPosition.z)*
            (1.0-smoothstep(.72,1.0,length(vec2(vertexPosition.x/.63,(vertexPosition.y-1.50)/.75))));
        groom=mix(1.0,.07,face);
    }
    return normal*(uFurLength*furLayer*groom)+(uFurBend+motion)*(furLayer*furLayer*groom);
}
