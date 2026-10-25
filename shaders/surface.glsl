uniform Material uMaterial;
uniform vec3 uTint, uEmissive;
uniform bool uLighting, uToon, uMultiTexture, uFog;
uniform sampler2D uTextures[4];
uniform bool uHasTexture[4];
uniform float uTextureMix, uAlphaCutoff, uOpacity;
uniform int uProcedural, uLightMask;

// Resolves textures and the optional antialiased procedural brick pattern.
vec4 sampleSurface(vec2 coordinates){
    vec4 surface = vec4(uMaterial.diffuse * uTint, 1.0);
    if (uHasTexture[0]){
        surface *= texture(uTextures[0], coordinates);
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
    if (surface.a < uAlphaCutoff){
        discard;
    }
    if (uMultiTexture){
        for (int i = 1; i < 4; ++i){
            if (uHasTexture[i]){
                surface.rgb *= mix(vec3(1.0), texture(uTextures[i], coordinates).rgb, uTextureMix);
            }
        }
    }

    return surface;
}

// Retains smooth shading normals and geometric receiver planes independently.
void surfaceNormals(out vec3 normal, out vec3 plane){
    normal = normalize(worldNormal);
    plane = cross(dFdx(worldPosition), dFdy(worldPosition));
    plane = length(plane) > 1e-8 ? normalize(plane) : normal;
    if (!gl_FrontFacing){
        normal = -normal;
    }
}
