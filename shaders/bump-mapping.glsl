// Converts the scalar height gradient to a world-space shading-normal perturbation.
vec3 heightNormal(vec3 normal, vec2 coordinates){
    vec2 uvX = dFdx(coordinates), uvY = dFdy(coordinates);
    vec2 stepUV = 1.0 / vec2(textureSize(uSurfaceMap, 0));
    float left = textureGrad(uSurfaceMap, coordinates - vec2(stepUV.x, 0), uvX, uvY).r;
    float right = textureGrad(uSurfaceMap, coordinates + vec2(stepUV.x, 0), uvX, uvY).r;
    float down = textureGrad(uSurfaceMap, coordinates - vec2(0, stepUV.y), uvX, uvY).r;
    float up = textureGrad(uSurfaceMap, coordinates + vec2(0, stepUV.y), uvX, uvY).r;
    vec2 gradientUV = vec2(right - left, up - down) / (2.0 * stepUV);
    vec3 positionX = dFdx(worldPosition), positionY = dFdy(worldPosition);
    vec3 acrossY = cross(positionY, normal), acrossX = cross(normal, positionX);
    float determinant = dot(positionX, acrossY);
    if (abs(determinant) < 1e-12){
        return normal;
    }
    vec3 gradient = (dot(gradientUV, uvX) * acrossY + dot(gradientUV, uvY) * acrossX) /
                    determinant;
    return normalize(normal - uHeightScale * gradient);
}
