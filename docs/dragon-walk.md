# Dragon walk-cycle phase correction

The original Blender `walk` action has keys in frames 1–40, but its cyclic curves
do not share a single 39-frame period. The front-leg controls repeat every 26
frames and the rear-leg controls every 39 frames. Their common period is 78
frames (3.25 seconds at 24 FPS).

Exporting only frames 1–40 and looping the result reset the front legs halfway
through their cycle. Walk is now baked over frames 1–79, including the matching
endpoint, from the original rig with its constraints evaluated.

Only Walk's animation stack, layer, curves and take range were replaced in
`models/dragon/Dragon.fbx`. Existing object identifiers and connections were
retained. Structural comparison of the binary FBX verified that geometry,
normals, UVs, materials, textures, skeleton, skin bindings and the Idle, Run and
Fly clips are unchanged. No playback-speed or interpolation workaround is used.

The skeletal-animation regression checks the complete period and vertex
continuity across the loop boundary for both original and subdivided geometry.
It failed with the truncated clip and passes with the complete cycle.
