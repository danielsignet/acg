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

The bottom toolbar contains clickable lights, shader selection, culling and the
patio/house camera view. Camera navigation follows
[Unity's flythrough controls](https://docs.unity.com/en-us/engine/6000.6/manual/unity-editor/editor-windows-views-reference/using-the-scene-view/scene-view-navigation):
hold the right mouse button and use WASD to move, Q/E for height and Shift for
speed. Releasing the mouse returns to the toolbar.

- Click the five lamp buttons, or press 1-5, to toggle the four colored lights and daylight.
- Click Shader, or press Z, to select the existing Phong or Toon shading mode.
  Like [Blender's shading menu](https://docs.blender.org/manual/en/5.4/editors/3dview/display/shading.html),
  Z opens a menu. Choose with the mouse, or use 1/2 while it is open.
- Click Culling, or press C, to switch both frustum and portal culling on/off.
- Click View to move between the patio and room A. Space resets the view;
  R reloads the scene and shaders. Esc closes an open menu first, then the viewer.

Shader selection applies to the whole scene and survives reload. Lamp and shader
buttons use the same application actions as their shortcuts. Development presets
are available through the command line instead of extra viewport shortcuts.

## View frustum culling

`CullVisitor` creates a list of geometry, model matrices and resolved states
without OpenGL calls. Six inward-facing planes reject AABBs outside the camera
volume. A rejected group skips its whole subtree; a group fully inside the
volume skips further plane tests. Touching bounds are retained conservatively.
Bounds are cached and invalidated through every DAG parent after geometry,
transform or child changes. Each LOD bound encloses all its levels.

Lights are collected separately so lights outside the camera still illuminate
visible surfaces. The observation preset stores both view and projection; camera movement,
resizing and LOD selection continue to use the live observer. Debug boxes are
green inside, yellow intersecting and red outside; the frozen volume is pink.
The HUD reports geometry draws, rejected subtrees, bound tests and CPU times for
culling and draw submission, excluding HUD/debug draws. CPU submission time is
not a GPU measurement.

The house spans x=-21..21 and z=-44..-16, with 14x14 rooms, five-unit walls and
3x3.5 doorways. Clear cross aisles connect all six rooms and the patio. Start
inside a front doorway with the View button at (-14, 2, -18), looking down -Z.
C compares draws. The `--portal-demo` preset provides the frozen overhead
inspection view with an open roof.

## Scene graph and portal culling

Walls are transparent by default. Their opacity changes only the rendering state;
the visibility graph continues to treat walls as solid. Only the actual 3x3.5
door openings transmit visibility. The XML `visibility` block describes six room
cells, seven internal openings and three entrances connecting to the patio.
Visibility clips each doorway to the incoming volume, then constructs new planes
through the eye and its clipped edges. Multiple door sequences may reach a room.
`CullVisitor` rejects a whole content subtree when it misses all valid volumes.
Floors, walls and ceilings remain as a reference shell when content is rejected.

The patio is an exterior graph region: solid walls also reject room objects when
viewed from outside, and an exit restricts which patio objects are visible from
inside. Lights are collected independently. Transparent walls render after opaque
geometry, sorted back to front, with alpha blending and depth writes disabled.

The observation preset freezes both the volume and the visibility eye. Its
observing camera and LOD remain live. `--portal-demo` provides an overhead view;
combine it with `--no-portals` to compare object visibility. Labels count each
room's actual object draws. `--solid-walls` changes wall appearance without
changing that list. These presets and detailed visibility switches are for
validation, while the viewport has a single culling toggle.

The layout models fixed walls. When a fly camera enters a wall or stands exactly
on a doorway boundary, visibility conservatively falls back. Removing the roof
also falls back for a visibility eye above the building; an observer above a
frozen indoor eye continues to show indoor culling. Scenes without a visibility
layout retain ordinary frustum culling.

## Graph visualization

Use `--dot <file>` to export the scene graph when loading or reloading.
Shared nodes and all LOD branches are included.

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
pixels with VFC off/on in 16 opaque house/patio view pairs and 16 transparent
pairs with roofs on and off, checks actual draw reduction, and exercises frozen
culling, debug rendering and reload. Transparent comparisons allow at most 1/255
channel rounding in less than 0.1% of pixels; opaque pairs must be exact.
The portal test independently checks 1,000 line/door intersections, solid wall
rejection, door sequences, exterior visibility and graph traversal. Rendering
validation also compares 20 exact image pairs with physically opaque walls and
portals off/on, plus two overhead views with an open roof, then checks that wall
transparency preserves the instance list
and that the transparent overhead demo visibly removes rejected objects.

## License

Based on Anders Backman/vrguru's VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original notices.
