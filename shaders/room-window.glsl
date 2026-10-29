uniform bool uRoomWindowEnabled;
uniform vec3 uRoomWindowMin, uRoomWindowMax;

// Clip triangles to the projected opening, rather than just testing bounds.
bool outsideRoomWindow(vec3 position){
    if (!uRoomWindowEnabled) return false;
    vec3 normal=step(abs(uRoomWindowMax-uRoomWindowMin),vec3(1e-4));
    vec3 ray=position-uEye;
    float denominator=dot(ray,normal);
    if (abs(denominator)<1e-6) return true;
    float t=dot(uRoomWindowMin-uEye,normal)/denominator;
    if (t<0.0 || t>1.0001) return true;
    vec3 hit=uEye+t*ray;
    return any(lessThan(hit,uRoomWindowMin-1e-3)) || any(greaterThan(hit,uRoomWindowMax+1e-3));
}
