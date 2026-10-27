#version 410 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexTexCoord;
layout(location = 7) in vec4 vertexTangent;
uniform mat4 uModel, uView, uProjection;
uniform mat3 uNormal;
uniform bool uNormalMapping, uHasNormalMap;
uniform float uTangentSign;
out vec3 worldPosition;
out vec3 worldNormal;
out vec4 worldTangent;
out vec2 texCoord;

// Transforms local positions and normals into world and clip space.
void main(){
    vec4 p = uModel * vec4(vertexPosition, 1.0);
    worldPosition = p.xyz;
    worldNormal = normalize(uNormal * vertexNormal);
    worldTangent = vec4(0);
    if (uNormalMapping && uHasNormalMap){
        vec3 tangent = mat3(uModel) * vertexTangent.xyz;
        tangent -= worldNormal * dot(worldNormal, tangent);
        float tangentLength = length(tangent);
        if (tangentLength > 1e-12){
            worldTangent = vec4(tangent / tangentLength, vertexTangent.w * uTangentSign);
        }
    }
    texCoord = vertexTexCoord;
    gl_Position = uProjection * uView * p;
}
