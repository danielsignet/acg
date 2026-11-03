uniform sampler2D uTerrainHeight;
uniform vec3 uTerrainDimensions;
uniform mat4 uModel;
// Explicit bilinear interpolation keeps picking and rasterization aligned, without
// hardware-dependent fixed-point interpolation of normalized texture samples.
float terrainHeight(vec2 uv){
    ivec2 size=textureSize(uTerrainHeight,0);
    vec2 pixel=clamp(uv,vec2(0),vec2(1))*vec2(size)-.5;
    ivec2 p=ivec2(floor(pixel));vec2 f=fract(pixel);
    float a=texelFetch(uTerrainHeight,clamp(p,ivec2(0),size-1),0).a;
    float b=texelFetch(uTerrainHeight,clamp(p+ivec2(1,0),ivec2(0),size-1),0).a;
    float c=texelFetch(uTerrainHeight,clamp(p+ivec2(0,1),ivec2(0),size-1),0).a;
    float d=texelFetch(uTerrainHeight,clamp(p+ivec2(1,1),ivec2(0),size-1),0).a;
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
vec3 terrainPoint(vec2 uv){
    return vec3((uv.x-.5)*uTerrainDimensions.x,
        terrainHeight(uv)*uTerrainDimensions.y,
        (uv.y-.5)*uTerrainDimensions.z);
}
vec3 terrainNormal(vec2 uv){
    vec2 t=1.0/vec2(textureSize(uTerrainHeight,0));
    float dx=(terrainPoint(uv+vec2(t.x,0)).y-terrainPoint(uv-vec2(t.x,0)).y)/(2*t.x*uTerrainDimensions.x);
    float dz=(terrainPoint(uv+vec2(0,t.y)).y-terrainPoint(uv-vec2(0,t.y)).y)/(2*t.y*uTerrainDimensions.z);
    return normalize(vec3(-dx,1,-dz));
}
