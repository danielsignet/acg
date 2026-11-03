uniform float uTreeHeight;
// xy: crown bend in metres, z: twist in radians, w: height-dependent crown spread.
uniform vec4 uTreeWarp;
vec3 treePosition(vec3 p){
    if(uTreeHeight<=0.0)return p;
    float h=max(p.y,0.0)/uTreeHeight,a=uTreeWarp.z*h;
    mat2 rotation=mat2(cos(a),-sin(a),sin(a),cos(a));
    p.xz=rotation*p.xz*max(.2,1.0+uTreeWarp.w*h)+uTreeWarp.xy*h*h;
    return p;
}
void deformTree(inout vec3 p,inout vec3 normal,inout vec3 tangent){
    if(uTreeHeight<=0.0)return;
    float h=max(p.y,0.0)/uTreeHeight,a=uTreeWarp.z*h;
    float spread=1.0+uTreeWarp.w*h,stretch=max(.2,spread);
    mat2 rotation=mat2(cos(a),-sin(a),sin(a),cos(a));
    vec2 q=rotation*p.xz;
    vec2 slope=((spread>.2?uTreeWarp.w:0.0)*q+stretch*uTreeWarp.z*vec2(q.y,-q.x)+2.0*uTreeWarp.xy*h)*
               (p.y>=0.0?1.0/uTreeHeight:0.0);
    vec2 transformedNormal=rotation*normal.xz/stretch;
    normal=vec3(transformedNormal.x,normal.y-dot(transformedNormal,slope),transformedNormal.y);
    tangent.xz=stretch*(rotation*tangent.xz)+slope*tangent.y;
    p.xz=q*stretch+uTreeWarp.xy*h*h;
}
