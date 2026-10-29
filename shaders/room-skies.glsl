#ifndef VR_ROOM_SKIES
#define VR_ROOM_SKIES
uniform int uEnvironmentCount, uSkyRegionCount;
uniform vec3 uSkyRegionMin[4], uSkyRegionMax[4];
uniform vec4 uSkyRegionCylinder[4];
uniform vec3 uSkyOpeningMin[4], uSkyOpeningMax[4], uSkyOpeningNormal[4];
uniform int uSkyRegionLayer[4];

bool insideSkyRegion(int i, vec3 point){
    if (any(lessThan(point,uSkyRegionMin[i]-1e-4)) ||
        any(greaterThan(point,uSkyRegionMax[i]+1e-4))) return false;
    vec2 offset=point.xz-uSkyRegionCylinder[i].xz;
    float radius=uSkyRegionCylinder[i].w;
    return radius==0.0 || dot(offset,offset)<=radius*radius+1e-4;
}

bool crossesSkyOpening(int i, vec3 origin, vec3 direction, out float distance){
    float denominator=dot(direction,uSkyOpeningNormal[i]);
    if (abs(denominator)<1e-6) return false;
    distance=dot(uSkyOpeningMin[i]-origin,uSkyOpeningNormal[i])/denominator;
    if (distance < -1e-4) return false;
    vec3 hit=origin+max(distance,0.0)*direction;
    return all(greaterThanEqual(hit,uSkyOpeningMin[i]-1e-4)) &&
           all(lessThanEqual(hit,uSkyOpeningMax[i]+1e-4));
}

int skyLayerAt(vec3 point){
    for (int i=0;i<uSkyRegionCount;++i)
        if (insideSkyRegion(i,point)) return uSkyRegionLayer[i];
    return 0;
}

// Select per ray, not per camera. A doorway can reveal a second sky in the
// same frame, including in mirror and cubemap captures and glass fallback.
int skyLayerForRay(vec3 origin, vec3 direction){
    for (int i=0;i<uSkyRegionCount;++i){
        if (!insideSkyRegion(i,origin)) continue;
        float distance;
        if (!(dot(direction,uSkyOpeningNormal[i])>0.0 &&
              crossesSkyOpening(i,origin,direction,distance))) return uSkyRegionLayer[i];
        break;
    }
    int layer=0;
    float closest=1e30;
    for (int i=0;i<uSkyRegionCount;++i){
        float distance;
        if (dot(direction,uSkyOpeningNormal[i])<0.0 &&
            crossesSkyOpening(i,origin,direction,distance) && distance<closest){
            layer=uSkyRegionLayer[i];closest=distance;
        }
    }
    return layer;
}
#endif
