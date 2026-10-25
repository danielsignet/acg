# Scene graph viewer

An interactive C++17/OpenGL 4.1 viewer for scene graphs, lighting and shading.
The default scene is `scenes/house-patio.xml`: six connected rooms with floors,
ceilings and open doorways, adjoining a larger patio containing the original
gallery. Use `--scene scenes/demo.xml` for the original gallery alone, or
`--scene <file>` to load another scene.

Requires CMake 3.21+ and an OpenGL 4.1 driver. Dependencies are bundled.
Run the commands below from the repository root.

## Build and run

### Windows

Install Visual Studio 2022 with C++ desktop tools and a Windows SDK.

```powershell
cmake --preset windows -S cmake/standalone
cmake --build build/windows --config RelWithDebInfo --parallel 4
& .\build\windows\bin\viewer.exe
```

### Linux / WSL

On Ubuntu/Debian:

```bash
sudo apt install build-essential cmake libgl-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake --preset linux -S cmake/standalone
cmake --build build/linux --parallel 4
./build/linux/bin/viewer
```

WSL needs a graphical session such as WSLg. Resources are copied beside the executable.

## Controls

- `WASD`: move; `Q/E`: down/up; `Shift`: move faster; right mouse button: look.
- `Space`: reset the view; `R`: reload the scene; `Esc`: quit.
- `P`: pause object animation; `L`: pause the light orbit; arrow keys: move the lights.
- `V`: toggle the LOD camera tour; `T`: toggle toon shading.
- `F2`: save a screenshot; `F3`: export the scene graph.
- `C`: toggle view frustum culling; `F`: freeze/unfreeze its camera volume.
- `K`: show bounding boxes and the frozen frustum; `H`: show/hide the house roof.
- `F1`: show/hide the HUD. Resetting or reloading releases the frozen frustum.
- `Tab`: move between the patio overview and the entrance of room A, keeping any
  frozen culling volume. `--inside` starts in room A; `--no-culling` starts with C off.

Lighting, texture and culling controls are shown in the viewer.

## View frustum culling

`CullVisitor` creates a list of geometry, model matrices and resolved states
without OpenGL calls. Six inward-facing planes reject AABBs outside the camera
volume. A rejected group skips its whole subtree; a group fully inside the
volume skips further plane tests. Touching bounds are retained conservatively.
Bounds are cached and invalidated through every DAG parent after geometry,
transform or child changes. Each LOD bound encloses all its levels.

Lights are collected separately so lights outside the camera still illuminate
visible surfaces. Freezing stores both view and projection; camera movement,
resizing and LOD selection continue to use the live observer. Debug boxes are
green inside, yellow intersecting and red outside; the frozen volume is pink.
The HUD reports geometry draws, rejected subtrees, bound tests and CPU times for
culling and draw submission, excluding HUD/debug draws. CPU submission time is
not a GPU measurement.

The house spans x=-21..21 and z=-44..-16, with 14x14 rooms, five-unit walls and
3x3.5 doorways. Clear cross aisles connect all six rooms and the patio. Start
inside a front doorway with Tab at (-14, 2, -18), looking down -Z. Toggle C to
compare draws, or freeze with F and move away with K enabled to inspect the
volume. H opens the roof for an overhead inspection. View frustum culling tests
the camera volume; visibility through walls will be handled by portal culling
in a later feature.

## Graph visualization

`F3` saves `acg-viewer/scenegraph.dot` in the system temporary directory. Use `--dot <file>`
to choose another output path. Shared nodes and all LOD branches are included.

With Graphviz installed, run this from the directory containing the exported file:

```sh
dot -Tsvg scenegraph.dot -o scenegraph.svg
```

## Tests

```powershell
ctest --test-dir build/windows -C RelWithDebInfo --output-on-failure
```

On Linux, use `ctest --test-dir build/linux --output-on-failure`.
The rendering test needs a graphical session.
The culling test covers bounds, shared-parent invalidation, all six planes,
2,000 randomized clip-space comparisons, state inheritance, per-path transforms
and LOD selection without a graphics context. The rendering test compares exact
pixels with VFC off/on in 16 house/patio views with roofs on and off, checks actual
draw reduction, and exercises frozen culling, debug rendering and reload.

## License

Based on Anders Backman/vrguru's VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original notices.
