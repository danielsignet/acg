# Scene graph viewer

An interactive C++17/OpenGL 4.1 viewer for scene graphs, lighting and shading.
The default scene is `scenes/demo.xml`; use `--scene <file>` to load another scene.

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

Lighting, texture and culling controls are shown in the viewer.

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

## License

Based on Anders Backman/vrguru's VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original notices.
