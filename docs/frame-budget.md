# Restoring the 60 FPS frame budget

The default scene exceeded the 16.67 ms budget. At 1280x800 on an i5-10600K and
GTX 1080, it averaged 22.36 ms without VSync and 34.30 ms (29.15 FPS) with VSync.
Animation was only part of the frame cost: repeated render-state work and driver
synchronization also prevented reaching the target.

The changes retain the same scene, shadow resolutions, lighting and effects:

- Collect only alpha-cutout, LOD and caster state for shadow passes, and collect
  transformed lights without resolving materials.
- Upload shared camera and light uniforms once per shader program and render pass;
  retain per-object light masks. Skip uploads when a program's uniform value is unchanged.
- Preserve caller state once around all shadow views and create fixed, validated
  framebuffers for the six cubemap faces instead of reattaching each frame.
- Track framebuffer, viewport, VAO and raster state while the viewer owns its GL
  context. This avoids repeated synchronous `glGet*` calls. Standalone library
  callers retain ordinary GL queries; external raw state changes can invalidate
  the viewer's cache explicitly.
- Avoid clearing the window buffer before offscreen work when the final pass
  already writes both color and depth.

| Unpaced default scene, performance view open | Before | After |
| --- | ---: | ---: |
| Mean frame | 22.36 ms | 14.39 ms |
| P95 frame | 25.65 ms | 15.76 ms |
| Mean FPS | 44.72 | 69.48 |
| Shadow CPU scope | 12.90 ms | 1.85 ms |
| Total GPU scope | 10.87 ms | 10.56 ms |

These are dynamic-scene measurements, not hardware-independent guarantees. Some
of the old shadow CPU scope was driver waiting rather than shadow computation.
The interactive viewer subsequently displayed 60 FPS with VSync enabled. A longer
visible run's screenshot showed 59.5 FPS / 16.81 ms averaged over 120 frames.

Raw measurements: [before](benchmarks/frame-before-unpaced.csv),
[before with VSync](benchmarks/frame-before-vsync.csv),
[after](benchmarks/frame-after-unpaced.csv).

`viewer --benchmark output.csv` enables profiling and exports the last 120 frames
after a default 240-frame run. Use `--hidden` or `--no-vsync` for unpaced measurements,
and `--frames N` for a different run length. Capturing a screenshot performs a
synchronous readback, so avoid `--screenshot` when measuring frame averages.

Regression cases were added for tracked GL state, deletion and restoration,
uniform array aliases, shared light placements, LOD and shadow caller-state
restoration. The final regression suite was not rerun: evaluation was stopped at
the user's request before committing and merging.
