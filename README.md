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

## Controls

| Input | Action |
| --- | --- |
| RMB + WASD | Look around and move the camera |
| RMB + Q/E | Move down/up |
| RMB + Shift | Move faster |
| 1-5 or light buttons | Toggle the four colored lights and daylight |
| Z or Shader | Select Phong with normal maps (1), plain Phong (N), Toon (2), or individual G-buffer views; use the mouse or menu keys 3-0 and A |
| G or Multiview | Show/hide all G-buffer channel previews together; enables deferred rendering; click a preview to expand it, click again to return |
| B or Bloom | Toggle bloom |
| AA button | Cycle Off → FXAA → MSAA 4x in expected cost order; works in forward and deferred and resets performance averages |
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
| Crosshair / Subdivide button | Aim at a highlighted model and toggle one cached subdivision level for that instance; generated in memory on first use |
| R | Reload the scene and shaders |
| Esc | Close the performance view or menu, then the program |

## Notice

Based on Anders Backman/vrguru's VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original notices.
