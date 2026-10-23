#version 410 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexTexCoord;
uniform mat4 uModel, uView, uProjection;
uniform mat3 uNormal;
out vec3 worldPosition;
out vec3 worldNormal;
out vec2 texCoord;

// Transforms local positions and normals into world and clip space.
void main(){
    vec4 p = uModel * vec4(vertexPosition, 1.0);
    worldPosition = p.xyz;
    worldNormal = normalize(uNormal * vertexNormal);
    texCoord = vertexTexCoord;
    gl_Position = uProjection * uView * p;
}
