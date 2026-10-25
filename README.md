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
- Click Shadows in the header, or press J, to toggle shadow mapping.
  Arrows move the lamp ring along X/Z; Page Up/Down moves it vertically.
- Click Culling, or press C, to switch both frustum and portal culling on/off.
- Press F to freeze the culling camera and show its volume. Move with RMB/WASD
  to inspect it from another view; F resumes live culling and Space resets.
- Click View to move between the patio and room A. Space resets the view;
  R reloads the scene and shaders. Esc closes an open menu first, then the viewer.

Shader selection applies to the whole scene and survives reload. Lamp and shader
buttons use the same application actions as their shortcuts. Development presets
are available through the command line instead of extra viewport shortcuts.

## Shadows

The shadow renderer uses a 2048-square directional
depth texture and a 1024-square point-light depth cubemap, with seven independent
light cameras attached through `Scene` and `RenderToTexture`. The first enabled
sun and point light cast shadows. Physical objects cast and receive them,
including alpha cutouts and transparent walls that remain solid in the visibility
model. Camera/portal culling cannot discard their physical shadow casters.

Use J or the Shadows button to compare; `--no-shadows` starts with the feature
disabled. Arrow keys and Page Up/Down move the existing colored lamps, including
the active shadow point light. The status line reports depth-pass draws separately.

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
## License

Based on Anders Backman/vrguru's VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original notices.
