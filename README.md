# ACG

A C++/OpenGL graphics implementation with model loading, materials, lighting and
XML scenes.

Requires CMake 3.x (3.21+) and an OpenGL 4.1 driver. GLFW, Assimp, zlib and FreeType
are built from bundled sources. Run the commands below from the repository root.

## Windows

Visual Studio 2022 with C++ desktop tools and a Windows SDK. In PowerShell:

```powershell
cmake --preset windows -S cmake/standalone
cmake --build build/windows --config RelWithDebInfo --parallel 4
$env:VR_PATH = (Get-Location).Path
& .\build\windows\bin\RelWithDebInfo\viewer.exe scenes/monkey.xml
```

## Linux / WSL

On Ubuntu, install the compiler and OpenGL/X11 development libraries:

```bash
sudo apt install build-essential cmake libgl1-mesa-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev
cmake --preset linux -S cmake/standalone
cmake --build build/linux --parallel 4
export VR_PATH="$PWD"
./build/linux/bin/viewer scenes/monkey.xml
```

WSL requires a graphical session, such as WSLg.

## Viewer

Pass a model file or an XML scene to `viewer`. Examples are in `models/` and
`scenes/`. `VR_PATH` points to the repository root for shaders, fonts and assets.

- `WASD`: move; `E/Q`: up/down; left `Shift`: move faster.
- Hold the left mouse button and drag to look around; scroll to change the field of view.
- `R`: reload the scene; `Esc`: quit.

License: [MIT](LICENSE).
