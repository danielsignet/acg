uniform float uBounceRadius;
uniform float uBounceTime;

// Volume-preserving squash at contact and smooth vertical rebound.
// The local bottom pole remains at -radius + lift throughout the cycle.
void bouncePose(out vec3 scale, out float offset){
    scale=vec3(1); offset=0;
    if (uBounceRadius<=0) return;
    float height=abs(sin(2.4*uBounceTime));
    float contact=height/.22;
    float sy=1-.3*exp(-contact*contact)+.06*height;
    scale=vec3(inversesqrt(sy),sy,inversesqrt(sy));
    offset=uBounceRadius*(sy-1+1.6*height);
}
