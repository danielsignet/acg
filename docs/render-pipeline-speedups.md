# Render pipeline speedups

All paths retain OpenGL 4.1 support, the original assets, material precision,
shadow resolution and all twelve source skin influences.

## Vertex data and streaming

The CPU vertex is 144 bytes, including skin joints and weights. Color and depth
passes now fetch a 48-byte `GpuVertex` containing position, normal, UV and tangent.
This reduces that vertex buffer by two thirds without quantizing its attributes.
Skin joints and weights live in separate immutable GPU texture buffers.

Dynamic vertex output, instance transforms, instance normal matrices and skin
palettes use three-slot `StreamBuffer` rings. Fences cover the last submitted use.
A busy slot gets fresh storage through buffer orphaning; the CPU never waits for
an in-flight slot or overwrites data still being read. No persistent mapping or
OpenGL 4.4 extension is required.

## Scene state and bounds

Traversal visitors retain up to 2,048 resolved inherited states, keyed by parent
and local state/material revisions. Uniform maps use copy-on-write storage, so
copies onto traversal stacks and render lists remain cheap. Per-frame shadow
uniforms use a live shared pass block rather than invalidating material state.
Setters invalidate cached state only when values actually change.

Each node retains up to eight world bounds keyed by its local bound revision and
the parent matrix. Shared meshes therefore retain separate bounds for distinct
placements. Mesh edits, skin poses and transform changes still invalidate their
dependent bounds. This does not remove traversal or visibility tests.

## One GPU skinning pass per changed pose

Transform feedback deforms each active submesh once per pose, before shadows and
color rendering. All shadow faces, reflections and color passes read the same
48-byte output. A paused pose performs no deformation. Subdivision retains the
same influences and uses the same path.

The CPU still evaluates exact skinned **positions**, once per changed pose, for
fitted shadow cameras, bounds and BVH picking. CPU normal/tangent skinning is lazy
and used only by explicit CPU vertex access or the fallback. Production rendering
does not read GPU vertices back. The separate `GPU skinning` and `Skin bounds`
profiler rows expose both costs.

`--cpu-skinning` selects the complete CPU path, including after scene reload.
Meshes exceeding texture-buffer limits automatically use that fallback.

## Shadow depth reuse

Each directional view and each point-light cubemap face tracks its own camera,
depth encoding, spot cone and static-caster generation. Skinning, update callbacks
on an ancestor, and render-target alpha textures mark casters dynamic. Other
casters are still checked for geometry, transform, alpha texture/cutoff, LOD
coverage, visibility and culling changes before reusing depth.

A changing view renders normally. After two consecutive frames with identical
view parameters, a separate static depth target is built lazily. Subsequent frames
copy static depth and render only visible dynamic casters. A face without dynamic
casters can reuse its completed depth image without clearing, copying or drawing.
When a dynamic caster leaves, the static depth is restored first, preventing
ghost shadows. Explicit writes through `RenderToTexture` invalidate full-face
reuse via a content revision.

This saves work for fixed lights and stable scene bounds. A light moving every
frame takes the normal render path. Exact fitting is retained, so changing scene
bounds can also invalidate a view. The cache trades memory for fewer draws: at
the default sizes, a directional cache uses 16 MiB and six cached point faces use
24 MiB per light. Allocation is lazy and bounded by allocated shadow views.

`--no-shadow-cache` selects complete redraws. `--no-scene-cache` disables resolved
state caches in the main, reflection and shadow traversals for comparisons. These
switches persist after reload. The performance view reports shadow cache hits,
fully reused faces and static depth rebuilds.

## Verification

`gpu_skinning` compares actual GPU output with CPU positions, normals, tangents
and UVs for all four dragon clips, twelve influences and subdivision. It also
checks picking, exact bounds, repeated render views and paused poses.
`shadow_cache` compares every depth pixel with an uncached renderer while moving
and hiding dynamic casters and changing static meshes, alpha coverage, lights,
spot direction and range. Existing forward/deferred, glass/reflection, alpha,
instancing, shadow and animation regressions run alongside these tests.
All 20 CTest suites pass. The streaming regression also queues GPU copies through
32 ring wraps and verifies every captured word after submission, and GPU skinning
is checked after releasing the current shader program.

## Measurements

28 October 2026, Windows x64, RelWithDebInfo, GeForce GTX 1080, 1280 × 800,
deferred rendering, environment reflections, AO and shadows enabled, VSync off.
Each process renders 360 frames; the CSV records the final 120-frame window.
No screenshot readback is included. Other GPU applications were not stopped.

| Case | Reference frame time | Optimized frame time | Shadow draws, reference → optimized |
| --- | --- | --- | --- |
| Default rotating lights, two runs | 16.14–16.65 ms | 16.51–16.63 ms | 484 → 451 |
| Fixed lights, one run | 16.79 ms | 13.40 ms | 476 → 91 |

The default case has **no demonstrated whole-frame improvement** in this A/B
comparison. CPU culling falls from 0.74–0.88 ms to 0.38–0.41 ms, but the frame
remains constrained by the rendering pipeline. Full CPU vertex skinning of
23,008 vertices is replaced by exact CPU positions (0.63–0.69 ms) and a separate
GPU pass. CPU submission timings can include driver backpressure; moving that
wait between profiler rows is not itself a frame-time saving.

With fixed lights, frame time is about **20% lower** in the recorded run. Twenty
four of twenty five views use static depth; sixteen faces are reused completely,
and shadow GPU time changes from 9.07 ms to 1.56 ms. This is a workload-specific
result, not a guaranteed speedup. The fixed-light fixture is a temporary copy of
`scenes/house-patio.xml` with only `name="lightOrbit"` renamed to
`name="fixedLightBenchmark"`, so the built-in orbit callback is not attached.
Original scene and model files are unchanged.

Raw CSVs: [default optimized](benchmarks/pipeline-default-optimized.csv),
[default reference](benchmarks/pipeline-default-reference.csv),
[optimized repeat](benchmarks/pipeline-default-optimized-repeat.csv),
[reference repeat](benchmarks/pipeline-default-reference-repeat.csv),
[fixed optimized](benchmarks/pipeline-fixed-optimized.csv),
[fixed reference](benchmarks/pipeline-fixed-reference.csv).

For timing, use the same scene, dimensions and quality settings, avoid screenshot
readback during the run, and let the 120-frame history replace startup frames:

```powershell
viewer --hidden --frames 360 --benchmark optimized.csv
viewer --hidden --frames 360 --cpu-skinning --no-shadow-cache --no-scene-cache --benchmark reference.csv
```

To reproduce the fixed-light fixture without editing the source scene:

```powershell
$fixedScene = Join-Path $env:TEMP 'acg-fixed-light-benchmark.xml'
$sceneText = [IO.File]::ReadAllText("$PWD/scenes/house-patio.xml")
[IO.File]::WriteAllText($fixedScene, $sceneText.Replace('name="lightOrbit"', 'name="fixedLightBenchmark"'))
viewer --root "$PWD" --scene "$fixedScene" --hidden --frames 360 --benchmark fixed-optimized.csv
viewer --root "$PWD" --scene "$fixedScene" --hidden --frames 360 --cpu-skinning --no-shadow-cache --no-scene-cache --benchmark fixed-reference.csv
```

The reference switches disable GPU skinning and the new caches. Both runs still
use packed vertices and stream rings; this is not a comparison with an older
binary. Other applications using the GPU can affect the result.
