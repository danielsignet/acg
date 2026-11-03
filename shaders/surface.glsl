uniform Material uMaterial;
uniform vec3 uTint, uEmissive;
uniform bool uLighting, uToon, uMultiTexture, uFog;
uniform bool uEmissionSwitchable, uEmissionEnabled;
uniform sampler2D uTextures[4];
uniform bool uHasTexture[4];
uniform sampler2D uSurfaceMap;
uniform bool uHasNormalMap, uHasHeightMap, uNormalMapping;
uniform float uNormalScale, uNormalY, uHeightScale;
uniform bool uParallax, uParallaxMapping, uHeightInAlpha, uBumpMapping;
uniform float uParallaxScale;
uniform float uFurLength;
uniform float uTextureMix, uAlphaCutoff, uOpacity;
uniform int uProcedural, uLightMask;
uniform int uReflectionProbe;
#include "lod-fade.glsl"

float surfaceHeight(vec2 coordinates, vec2 uvX, vec2 uvY){
    vec4 value = textureGrad(uSurfaceMap, coordinates, uvX, uvY);
    return uHeightInAlpha ? value.a : value.r;
}
#include "bump-mapping.glsl"
#include "parallax-mapping.glsl"

// Switches tagged emitters while retaining unrelated emissive materials.
vec3 surfaceEmission(){
    return uEmissionSwitchable && !uEmissionEnabled ? vec3(0) : uEmissive;
}

// Resolves textures and the optional antialiased procedural brick pattern.
vec4 sampleSurface(vec2 coordinates){
    applyLodFade();
    vec4 surface = vec4(uMaterial.diffuse * uTint, 1.0);
    if (uHasTexture[0]){
        surface *= textureGrad(uTextures[0], coordinates, dFdx(texCoord), dFdy(texCoord));
    }
    if (uProcedural == 1){
        vec2 brick = coordinates * vec2(10.0, 6.0);
        brick.x += mod(floor(brick.y), 2.0) * 0.5;
        vec2 edge = min(fract(brick), 1.0 - fract(brick));
        vec2 width = max(fwidth(brick), vec2(0.001));
        float interior =
            smoothstep(0.035, 0.035 + width.x, edge.x) * smoothstep(0.055, 0.055 + width.y, edge.y);
        float variation = 0.88 + 0.12 * sin(dot(floor(brick), vec2(17.0, 43.0)));
        surface.rgb *= mix(vec3(0.3, 0.32, 0.34), vec3(0.95, 0.5, 0.3) * variation, interior);
    }
    if(uProcedural==2){
        vec3 p=worldPosition;
        float strata=sin(p.y*7.0+p.x*.8+sin(p.z*1.6)*1.3+sin(p.x*3.1+p.z*2.3)*.25);
        float vein=1.0-smoothstep(.02,max(.06,fwidth(strata)*1.5),abs(strata));
        float stone=.86+.14*sin(dot(p,vec3(4.1,5.3,3.7)));
        surface.rgb*=stone*mix(vec3(.88,.91,.95),vec3(.47,.40,.32),vein*.65);
    }
    if (surface.a < uAlphaCutoff){
        discard;
    }
    if (uMultiTexture){
        for (int i = 1; i < 4; ++i){
            if (uHasTexture[i] && !(i==2 && uFurLength>0.0)){
                surface.rgb *= mix(vec3(1.0),
                    textureGrad(uTextures[i], coordinates, dFdx(texCoord), dFdy(texCoord)).rgb,
                    uTextureMix);
            }
        }
    }

    return surface;
}

// Retains smooth shading normals and geometric receiver planes independently.
void surfaceNormals(vec2 coordinates, out vec3 normal, out vec3 plane){
    normal = normalize(worldNormal);
    plane = cross(dFdx(worldPosition), dFdy(worldPosition));
    plane = length(plane) > 1e-8 ? normalize(plane) : normal;
    vec3 tangent = worldTangent.xyz - normal * dot(normal, worldTangent.xyz);
    if (uNormalMapping && uHasNormalMap && abs(worldTangent.w) > 0.5 && length(tangent) > 1e-8){
        tangent = normalize(tangent);
        vec3 bitangent = cross(normal, tangent) * sign(worldTangent.w);
        vec3 mapped = textureGrad(uSurfaceMap, coordinates, dFdx(texCoord), dFdy(texCoord)).xyz * 2.0 - 1.0;
        mapped.xy *= uNormalScale;
        mapped.y *= uNormalY;
        if (dot(mapped, mapped) > 1e-8){
            normal = normalize(mat3(tangent, bitangent, normal) * mapped);
        }
    } else if (uNormalMapping && uHasHeightMap && uBumpMapping){
        normal = heightNormal(normal, coordinates);
    }
    if (!gl_FrontFacing){
        normal = -normal;
    }
}
