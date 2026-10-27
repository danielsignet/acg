uniform vec2 uLodRange;

// Uses complementary stable pixel coverage so overlapping LODs retain opaque depth writes.
void applyLodFade(){
    if (uLodRange.x <= 0.0 && uLodRange.y >= 1.0){
        return;
    }
    float threshold = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    if (threshold < uLodRange.x || threshold >= uLodRange.y){
        discard;
    }
}
