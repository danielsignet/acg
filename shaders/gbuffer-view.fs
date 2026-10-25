#version 410 core
in vec2 texCoord;
out vec4 fragmentColor;
uniform sampler2D uColor, uPosition, uNormal, uAlbedo, uSpecular, uDepth, uAmbient, uEmissive,
    uPlane;
uniform mat4 uInverseProjection;
uniform int uViewMode;

// Displays individual G-buffer channels and restores opaque depth for later overlays.
void main(){
    float depth = texture(uDepth, texCoord).r;
    gl_FragDepth = depth;
    vec4 position = texture(uPosition, texCoord);
    vec3 color = texture(uColor, texCoord).rgb;
    if (uViewMode > 0){
        color = vec3(0.025, 0.035, 0.05);
        if (position.w > 0.0){
            if (uViewMode == 1){
                color = clamp(vec3(0.5) + position.xyz * 0.02, 0.0, 1.0);
            } else if (uViewMode == 2){
                color = normalize(texture(uNormal, texCoord).xyz) * 0.5 + 0.5;
            } else if (uViewMode == 3){
                color = texture(uAlbedo, texCoord).rgb;
            } else if (uViewMode == 4){
                color = clamp(texture(uSpecular, texCoord).rgb, 0.0, 1.0);
            } else if (uViewMode == 6){
                color = texture(uAmbient, texCoord).rgb;
            } else if (uViewMode == 7){
                color = texture(uEmissive, texCoord).rgb;
            } else if (uViewMode == 8){
                color = normalize(texture(uPlane, texCoord).xyz) * 0.5 + 0.5;
            } else{
                vec4 view = uInverseProjection * vec4(texCoord * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                color = vec3(1.0 - exp(-abs(view.z / view.w) * 0.035));
            }
        }
    }
    fragmentColor = vec4(color, 1.0);
}
