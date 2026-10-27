uniform sampler2D uDepth;
uniform mat4 uInverseProjection;
uniform float uFocusDistance;

// Reconstructs positive camera-space depth for perspective and orthographic cameras.
float viewDepth(vec2 uv){
    vec4 position = uInverseProjection *
        vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r * 2.0 - 1.0, 1.0);
    return abs(position.z / position.w);
}

// Bounds the lens radius in screen pixels so its appearance survives viewport resizing.
float maxDofRadius(){
    return float(textureSize(uDepth, 0).y) * 0.0125;
}

// Uses inverse depth as in a thin lens: negative radii are in front of the focal plane.
float dofRadius(float depth){
    float radius = 2.0 * maxDofRadius() * (depth - uFocusDistance) / max(depth, 0.001);
    return sign(radius) * clamp(abs(radius) - 0.5, 0.0, maxDofRadius());
}
