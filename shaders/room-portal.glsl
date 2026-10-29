uniform bool uRoomPortal;
uniform mat4 uRoomPortalViewProjection;

vec3 roomPortalColor(vec3 position){
    vec4 projected=uRoomPortalViewProjection*vec4(position,1);
    vec2 uv=projected.xy/projected.w*.5+.5;
    return texture(uTextures[2],uv).rgb;
}
