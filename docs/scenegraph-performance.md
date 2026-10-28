# Scene graph performance

The implementation retains the scene DAG and its separate update, cull and render visitors.
It addresses inactive mesh deformation, independent animated instances, opaque color-pass
instancing and reuse of derived transforms and materials. It requires OpenGL 4.1.

## Animation and model variants

`ModelInstance` owns its animation controller. Instances of an animated asset share immutable
skeleton/clip data and rest vertices, while owning independent playback time, skin palettes,
deformed vertices and index/GPU buffers. Static instances continue sharing their original
asset nodes and geometry. Imported joint callbacks are rebound to the instance's controller.

The update visitor advances the instance controller and traverses its selected mesh variant.
The cached inactive original/subdivision branch performs no skinning. Selecting a variant
immediately synchronizes its joint transforms and surface with the current pose, including
while playback is paused. The instance bound includes its active variant and any additional
attachments; attached nodes retain their updates and culling exemptions.

Explicitly invisible model instances advance their pose but skip surface deformation.
Camera frustum/portal rejection does not suppress animation updates: off-camera objects can
still contribute to shadows, mirrors and local reflection captures. This change does not
introduce view-dependent animation skipping.

## Color-pass instancing

After culling, compatible opaque instances are sorted and submitted together in both forward
and deferred rendering, including auxiliary color views using `RenderVisitor`. Compatibility
includes mesh, program, room, material values, textures, every uniform, light masks and raster
state. Hash collisions cannot merge incompatible states because exact equality is also checked.
Rooms remain separate batches so draw counts can still be attributed to one room.

The coarse material ordering excludes transient pass uniforms and shadow-target allocations.
This keeps order stable when headlights allocate additional depth targets and are switched off
again. Transparent objects retain stable back-to-front submission; refractive glass is submitted
individually. Programs without the instancing attribute/uniform contract use individual draws.

Per-instance normal matrices and tangent signs are calculated on the CPU using the same path
as individual draws, then streamed as attributes. This preserves nonuniform, mirrored and
sheared transforms without doing an inverse per vertex. Shadow instancing retains its existing
matrix-only upload path. `RenderStats::draws` counts real GPU submissions, `instances` counts
submitted geometry occurrences, and triangle counts still include every instance.

## Bounded caches

- `Transform` reuses world matrices keyed by the complete parent matrix, and invalidates them
  after a changed local matrix. Distinct DAG placements therefore remain distinct. Writing the
  same local matrix does not dirty bounds or advance the transform revision.
- `Geometry` reuses model-space-to-world normal matrices and orientation signs keyed by the
  complete model matrix.
- Inherited material resolution reuses the resolved material while parent, local and resolved
  output revisions match. Setters and assignment invalidate the relevant result. Uniforms and
  other `State` values are still resolved normally; this is not a cache of the entire render state.

Each cache retains at most eight entries per owning object. Shared resources let these cached
results be reused between color, shadow and reflection traversals. The existing graph update
and traversal remain single-threaded.

## Measurements

Measured on 28 October 2026, Intel Core i5-10600K, Windows x64, MSVC 19.38,
RelWithDebInfo. The baseline is commit `e4d4f4f`; the after measurements use commit `e430a48`.
Subsequent detailed profiling and the skinning compiler optimization are documented in
[animation profiling](animation-profiling.md).
The benchmark loads the unchanged `Dragon.fbx` and runs 40 warm-up steps followed by 300
samples per phase at a fixed 1/60-second animation delta. Source animation starts in Idle.
Loading, subdivision construction, rendering and GPU transfers are outside the timings.
Bounds and render-list collection are timed separately from update.

| Phase | Baseline update mean | After update mean, two runs |
| --- | ---: | ---: |
| Original animated | 3.136 ms | 3.163–3.185 ms |
| Original explicitly hidden, animated | 3.210 ms | 0.161–0.166 ms |
| Subdivided variant animated | 15.008 ms | 12.104–14.439 ms |
| Original animated after subdivision was created | 15.020 ms | 3.163–3.186 ms |
| Original paused after subdivision was created | 0.053 ms | 0.030–0.030 ms |

The primary regression is removed: returning to the original mesh no longer retains the
subdivision's deformation cost, reducing this phase's CPU update by about 79%. Bounds in that
phase decrease from 1.140 ms to 0.230–0.249 ms. Subdivided-case timings varied between runs;
the raw results are retained instead of claiming a fixed speedup for that phase. These are
CPU component measurements, not end-to-end frame-time or FPS measurements.

The dedicated eight-object rendering regression reduces color draws from **8 to 4** in both
forward and deferred modes while preserving instance/triangle counts and rendered pixels
within a two-channel-value rounding tolerance. This is a draw-count demonstration rather
than a whole-scene GPU-time benchmark.

Raw data: [baseline](benchmarks/scenegraph-before.csv), [after](benchmarks/scenegraph-after.csv),
[after repeat](benchmarks/scenegraph-after-repeat.csv).

Build and reproduce from the repository root:

```powershell
cmake --build build/windows --config RelWithDebInfo --target scenegraph_cpu_benchmark --parallel 1
& ./build/windows/RelWithDebInfo/scenegraph_cpu_benchmark.exe "$PWD" "$PWD/build/windows/scenegraph-cpu.csv"
ctest --test-dir build/windows -C RelWithDebInfo --output-on-failure
```

`scenegraph_cpu_benchmark` is excluded from the default build and from CTest so variable wall
times never become pass/fail assertions. Its source is in `benchmarks/scenegraph_cpu.cpp`.

## Regression coverage

All 18 CTest suites pass. Added checks cover independent playback and shared source data,
inactive-variant skin revisions, immediate paused variant switching, hidden/resumed models,
attached nodes, DAG transform caching, material edits/assignment, and matching forward/deferred
images with normal maps, tint overrides, light masks, LOD coverage, transparency and mirrored,
nonuniform and sheared transforms. Existing application validation also checks exact headlight
restoration, shadows, mirrors, cubemap capture, culling, picking and scene reload.

Spatial BVHs above instances, HLOD, general occlusion, GPU skinning and dirty shadow/probe
scheduling remain separate future features; they are not prerequisites for these changes.
