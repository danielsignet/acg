// Narkowicz's ACES filmic fit maps linear HDR to display values before gamma encoding.
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/ (CC0)
vec3 displayColor(vec3 radiance){
    vec3 x = max(radiance, vec3(0));
    vec3 mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    return pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));
}
