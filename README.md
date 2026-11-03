# ACG

Fork of Anders Backman/vrguru's VRLib. C++17 and OpenGL 4.1.
Dependencies and runtime assets are bundled.

## Build and run

Windows requires CMake 3.21+, Visual Studio 2022 with C++ desktop tools and a Windows SDK.
Run from the repository root:

```powershell
cmake --preset windows -S cmake/standalone
cmake --build build/windows --config RelWithDebInfo --parallel 1
.\build\windows\bin\viewer.exe
.\build\windows\bin\pantheon.exe
```

On Ubuntu/Debian with a graphical session (WSLg is supported):

```bash
sudo apt install build-essential cmake libgl-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake --preset linux -S cmake/standalone
cmake --build build/linux --parallel 4
./build/linux/bin/viewer
./build/linux/bin/pantheon
```

`viewer` opens the Patio scene; `pantheon` opens Pantheon. Models and textures
require no additional downloads, asset generation, Git LFS or submodules.

For a portable Windows installation:

```powershell
cmake --install build/windows --config RelWithDebInfo --prefix installed
.\installed\pantheon.exe
```

Move the complete installation folder, including its resource directories and DLLs.

## Controls

| Input | Action |
| --- | --- |
| RMB + mouse / WASD / Q,E | Look / move / change height; Shift moves faster |
| Space / R / Esc | Reset camera / reload scene and shaders / close menu or exit |
| F1 / F2 / F3 | Performance view / forward or deferred / Off, FXAA or MSAA 4x |
| F4 | Toggle subdivision for the targeted model |
| F5 / F6 | Reflection mode / normal and height bump mapping |
| F7 / F8 / F9 | VSync / deferred light culling / parallax mapping |
| F11 / F12 | Fur / star particles |
| P | Pause or resume the targeted basketball or doughnut |
| F | Freeze scene animation and culling camera |
| C / J / L | Culling / shadows / flashlight |
| O / B / V | Ambient occlusion / bloom / depth of field |
| Z / G / Tab | Shader menu / buffer previews / hide menus |
| 1-5 / T | Toggle lights / pause light orbit |
| Arrows / Page Up,Down | Move the light ring |
| Shader menu: 1 / N / 2 / H / K | Phong / plain Phong / Toon / Blinn-Phong / Cook-Torrance |

## Notice

Based on VRLib under the [MIT license](LICENSE).
Bundled dependencies and assets retain their original licenses and notices.

Dragon model, rig and animations: [3DHaupt](https://blendswap.com/blend/10300), CC BY 3.0.
Sky HDRI: [Kloofendal 48d Partly Cloudy (Pure Sky)](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky), Greg Zaal and Jarod Guest, CC0.
Bear: [Frakassets/Pixor](https://pixor.art/library/creatures/bear/), CC0.
Filmic tone curve: [Krzysztof Narkowicz](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/), CC0.
