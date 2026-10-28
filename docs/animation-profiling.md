# Animation profiling

The former `Input / animation` percentage combined GLFW events/commands with the entire
application update. It could not distinguish input, evaluating a skeleton and deforming its
mesh. The F1 view now presents these CPU stages separately:

1. Window events / commands, with CPU subdivision attributed to its own row.
2. Input / camera movement.
3. Animation track sampling: finding/interpolating position, rotation and scale keys.
4. Skeleton transforms: local TRS matrices and parent-to-child world transforms.
5. Skin palettes: mesh-local joint matrices and inverse-transpose normal matrices.
6. CPU vertex skinning: deformation of positions, normals and tangents.
7. Remaining scene graph traversal and callbacks, including joint synchronization.

Existing rendering stages follow those rows. Animation stages are nested inside the measured
scene update, so their times are subtracted from the remaining-scene row. The update total is
not added a second time. Input is outside that total. CPU-only stages display `-` in the GPU
column. Paused or already synchronized meshes record zero skinning work; the view also shows
processed vertex and joint-influence counts per frame.

`UpdateContext` carries an optional profiler through callbacks; disabled profiling performs
no timestamp queries. The benchmark uses `FrameProfiler(false)` to collect CPU scopes without
creating an OpenGL context. The finite history still serves the live UI, while the benchmark
aggregates all 300 measured samples rather than only the last 120 stored frames.

## Findings

Hardware/build: Intel Core i5-10600K, MSVC 19.38, Windows x64, RelWithDebInfo, 28 October 2026.
Each phase uses 40 warm-up updates followed by 300 samples with a fixed 1/60-second delta.
Loading, subdivision construction and rendering are excluded. Bounds are measured separately.

With the previous compilation settings, the original Idle model took about **3.2 ms** to
update. Approximately **3.0 ms (93%)** was vertex skinning. Track sampling, skeleton transforms
and skin palettes together took about **0.2 ms**. Paused meshes have no repeated skinning;
an explicitly hidden model still evaluates its pose but performs zero vertex deformation.

The asset's workload is:

| Property | Original | Subdivided |
| --- | ---: | ---: |
| Skeleton joints / animation tracks | 168 / 166 | 168 / 166 |
| Mesh-palette joint entries | 495 | 495 |
| Vertices processed per animated update | 23,008 | 82,178 |
| Active joint influences processed | 54,667 | 225,950 |
| Average influences per vertex | 2.376 | 2.750 |
| Stored bytes per vertex | 144 | 144 |

For each active influence, the CPU transforms a position, normal and tangent and accumulates
their weighted values. It then normalizes and orthogonalizes the resulting frame per vertex.
Subdivision therefore increases joint-influence work by about **4.13 times**, even though
the skeleton and its keyframe tracks are unchanged. This is the dominant scaling factor.
Deformation is performed once per changed mesh pose and reused by color/shadow/reflection
passes; it is not repeated once per shadow view.

There was also a significant compiler factor. CMake's MSVC RelWithDebInfo configuration used
`/O2 /Ob1`. A separate executable compiling just the same skinning implementation with `/Ob2`
reduced the measured kernel time substantially. The only production compiler change now applies
`/Ob2` to `GeometrySkin.cpp` in MSVC RelWithDebInfo; debug symbols, floating-point settings and
other translation units retain their settings. Release already permits this inlining level.
[Microsoft documents `/Ob1` and `/Ob2`](https://learn.microsoft.com/en-us/cpp/build/reference/ob-inline-function-expansion?view=msvc-170):
the latter allows the compiler to consider more functions for expansion. The measurements
establish the benefit for this GLM-heavy kernel without claiming a specific assembly-level
instruction breakdown.

The final reference and production targets reproduced the result:

| CPU stage | Reference `/Ob1` | Production `/Ob2` |
| --- | ---: | ---: |
| Original Idle: vertex skinning | 3.124 ms | 1.178 ms |
| Original Idle: whole scene update | 3.392 ms | 1.404 ms |
| Subdivided Idle: vertex skinning | 11.816 ms | 4.629 ms |
| Subdivided Idle: whole scene update | 12.079 ms | 4.885 ms |
| Original Fly: vertex skinning | 2.930 ms | 1.216 ms |

These are CPU component timings, not FPS gains or GPU timings. Independent initial A/B runs
also measured approximately 3.0 versus 1.2–1.45 ms for original skinning and 11.9 versus 4.7 ms
for subdivided skinning. The compiler change accounts for roughly a 2.5-fold improvement in
the expensive kernel on this machine.

The current interleaved `Vertex` also carries static UVs and 12 joint-index/weight slots.
Animated draws upload that entire 144-byte record, although only positions, normals and tangents
are deformed. That transfer remains inside geometry/shadow rendering stages, outside the CPU
skinning row. Separating static and dynamic GPU streams is a further measurable opportunity;
this change does not alter that buffer layout or move deformation to the GPU.

## Reproduction and validation

```powershell
cmake --build build/windows --config RelWithDebInfo --target animation_profile animation_profile_reference --parallel 1
& ./build/windows/RelWithDebInfo/animation_profile.exe "$PWD" "$PWD/build/windows/animation-production.csv"
& ./build/windows/RelWithDebInfo/animation_profile_reference.exe "$PWD" "$PWD/build/windows/animation-reference.csv"
ctest --test-dir build/windows -C RelWithDebInfo --output-on-failure
```

The reference target is MSVC-only, excluded from the default build and from CTest. It compiles
the same `GeometrySkin.cpp` directly into the executable with `/Ob1`, so its implementation
is used instead of the optimized copy in `vrlib`. Both executables use the same profiling
code, asset, skeleton implementation and fixed animation timeline. Run them sequentially
without the interactive viewer competing for resources.

Final data: [reference](benchmarks/animation-reference.csv),
[production](benchmarks/animation-production.csv).
Initial repeated experiment: [Ob1](benchmarks/animation-breakdown-ob1.csv),
[Ob1 repeat](benchmarks/animation-breakdown-ob1-repeat.csv),
[Ob2](benchmarks/animation-breakdown-ob2.csv),
[Ob2 repeat](benchmarks/animation-breakdown-ob2-repeat.csv).

Regression checks verify disjoint CPU percentages, independent input and animation stages,
CPU-only labels, work counters and zero work on synchronized paused/hidden meshes. Existing
skeletal, picking, subdivision and rendering suites verify behavior with the optimized kernel.
