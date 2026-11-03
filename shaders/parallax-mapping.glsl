// Trace into a height field below the mesh plane. Height white is the top;
// a negative scale reverses the relief. Scale is measured in world units.
vec2 parallaxCoordinates(vec2 coordinates){
    if (!uParallax || !uParallaxMapping || !uNormalMapping || !uHasHeightMap ||
        uParallaxScale == 0.0){
        return coordinates;
    }
    // Evaluate derivatives before any per-fragment facing/degeneracy early exit.
    vec2 uvX = dFdx(coordinates), uvY = dFdy(coordinates);
    vec3 positionX = dFdx(worldPosition), positionY = dFdy(worldPosition);
    vec3 n = normalize(worldNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 eye = uEye - worldPosition;
    float eyeLength = length(eye);
    if (eyeLength < 1e-6) return coordinates;
    vec3 view = eye / eyeLength;
    float facing = dot(n, view);
    // Fade at the silhouette instead of allowing unbounded grazing-angle offsets.
    if (facing <= 0.04) return coordinates;
    vec3 acrossY = cross(positionY, n), acrossX = cross(n, positionX);
    float determinant = dot(positionX, acrossY);
    float uvDeterminant = uvX.x * uvY.y - uvX.y * uvY.x;
    if (abs(determinant) < 1e-15 || abs(uvDeterminant) < 1e-15) return coordinates;
    // The inverse surface Jacobian preserves UV tiling, mirrored charts and model scale.
    vec2 ray = (uvX * dot(view, acrossY) + uvY * dot(view, acrossX)) / determinant;
    ray *= abs(uParallaxScale) * smoothstep(0.04, 0.15, facing) / max(facing, 0.08);
    // A view-angle budget alone can skip narrow mortar/brick edges when depth
    // is amplified. Bound the step in height-map texels as well as by angle.
    float rayTexels=length(ray*vec2(textureSize(uSurfaceMap,0)));
    int layers=int(clamp(max(mix(48.0,16.0,clamp(facing,0.0,1.0)),ceil(rayTexels*3.0)),16.0,512.0));
    float low = 0.0, high = 0.0;
    float initialHeight = surfaceHeight(coordinates, uvX, uvY);
    if ((uParallaxScale > 0.0 ? 1.0 - initialHeight : initialHeight) <= 0.0)
        return coordinates;
    for (int step = 1; step <= 512; ++step){
        high = float(step) / float(layers);
        float height = surfaceHeight(coordinates - ray * high, uvX, uvY);
        float depth = uParallaxScale > 0.0 ? 1.0 - height : height;
        if (high >= depth || step >= layers) break;
        low = high;
    }
    // Refine only the first crossing bracket, retaining nearer occluding ridges.
    for (int step = 0; step < 5; ++step){
        float middle = (low + high) * 0.5;
        float height = surfaceHeight(coordinates - ray * middle, uvX, uvY);
        float depth = uParallaxScale > 0.0 ? 1.0 - height : height;
        if (middle < depth) low = middle;
        else high = middle;
    }
    return coordinates - ray * ((low + high) * 0.5);
}
