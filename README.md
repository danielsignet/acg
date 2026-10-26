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
| Z or Shader | Select Phong, Toon, positions, normals, albedo, specular or depth; use the mouse or 1-7 in the menu; 8/9/0 show ambient, emissive and geometric normals |
| G | Show/hide G-buffer previews; click a preview to expand it, click again to return |
| B or Bloom | Toggle bloom |
| V or DoF | Toggle depth of field; aim the center marker at the surface to focus on |
| J or Shadows | Toggle shadows |
| L or Flashlight | Toggle the camera-mounted flashlight |
| Arrows / Page Up / Page Down | Move the lights along X/Z and Y |
| C or Culling | Toggle frustum and portal culling |
| F1 | Show/hide performance: CPU/GPU time and percentage breakdown; Reset averages clears measurements after changing settings |
| F | Freeze/resume the culling camera and show/hide its debug volumes |
| View | Switch between patio and house |
| Space | Reset the camera |
| R | Reload the scene and shaders |
| Esc | Close the performance view or menu, then the program |

## Notice

Based on Anders Backman/vrguru's VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original notices.
