# ACG

## Installation

Requires CMake 3.21+ and OpenGL 4.1. Dependencies are bundled.
Run the commands from the repository root.

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

WSL requires a graphical session such as WSLg.

The scene graph now keeps independent animation playback per model instance, updates only
the active original/subdivision mesh variant, and batches compatible opaque instances in
forward and deferred color passes. Transform, normal-matrix and inherited-material caches
reuse unchanged data across passes. See [performance verification](docs/scenegraph-performance.md)
for the measurements, regression coverage and benchmark command.
The performance view separates input, animation sampling, skeleton transforms, skin palettes,
CPU vertex skinning and remaining scene work; see [animation profiling](docs/animation-profiling.md).

## Controls

| Input | Action |
| --- | --- |
| RMB + WASD | Look around and move the camera |
| RMB + Q/E | Move down/up |
| RMB + Shift | Move faster |
| 1-5 or light buttons | Toggle the four colored lights and daylight |
| Z or Shader | Select Phong with surface detail (1), plain Phong (N), Toon (2), Blinn–Phong (H), Cook–Torrance (P), or individual G-buffer views; use the mouse or menu keys 3-0 and A |
| G or Multiview | Show/hide all G-buffer channel previews together; enables deferred rendering; click a preview to expand it, click again to return |
| B or Bloom | Toggle bloom |
| F3 or Anti-aliasing | Cycle Off → FXAA → MSAA 4x in expected cost order; works in forward and deferred and resets performance averages |
| F5 or Reflections | Cycle Off → Environment (HDRI) → Cubemaps (local scene captures); preserves diffuse skylight, disables the planar mirror in Off, and resets performance averages |
| F6 or Bump maps | Toggle height-map surface relief; materials without height maps retain their normal maps, and plain Phong disables both; resets performance averages |
| V or DoF | Toggle depth of field; focus follows the surface under the crosshair, and no target leaves the image sharp |
| J or Shadows | Toggle shadows |
| L or Flashlight | Toggle the camera-mounted flashlight |
| Arrows / Page Up / Page Down | Move the lights along X/Z and Y while unfrozen |
| C or Culling | Toggle frustum and portal culling |
| F2 or renderer button | Switch between deferred and forward; AO, post effects and G-buffer views use deferred |
| O or AO | Toggle screen-space ambient occlusion; Z then A shows AO alone |
| F1 | Show/hide performance: CPU/GPU time and percentage breakdown; Reset averages clears measurements after changing settings |
| F | Freeze/resume the culling camera, model animations and moving lights; show/hide the camera's debug volumes |
| Space | Reset the camera |
| F4 or Subdivide | Aim at a highlighted model and toggle one cached subdivision level for that instance; generated in memory on first use; No target when nothing is selected |
| Headlights button by the crosshair | Shown when targeting the car; toggle its headlamps and light beams, initially off |
| Animation button by the crosshair | Shown for animated models; cycle source animation clips; the dragon starts in Idle, and F pauses/resumes playback |
| Tab or Hide menus | Hide/show the menu bars and buffer previews; crosshair and target actions stay visible, including in F1 |
| R | Reload the scene and shaders |
| Esc | Close the performance view or menu, then the program |

## Notice

Based on Anders Backman/vrguru's VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original notices.

Dragon model, rig and animations: [3DHaupt](https://blendswap.com/blend/10300), [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/). The bundled `models/dragon/Dragon.fbx` has been replaced with an animated export from `BGE Dragon 2.07.blend`. Rig constraints are baked; the original mesh topology is preserved without subdivision.

Sky HDRI: [Kloofendal 48d Partly Cloudy (Pure Sky)](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky), Greg Zaal and Jarod Guest, [CC0](https://polyhaven.com/license).
Filmic tone curve: [Krzysztof Narkowicz](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/), CC0.
