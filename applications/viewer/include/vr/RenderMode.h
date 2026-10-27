#pragma once

namespace vr{
enum class AntialiasingMode{ Off, Fxaa, Msaa };

// Cycles from the lowest expected rendering cost to four-sample hardware antialiasing.
inline AntialiasingMode nextAntialiasing(AntialiasingMode mode){
    return static_cast<AntialiasingMode>((static_cast<unsigned>(mode) + 1) % 3);
}

// Names the active technique and its sample count for the toolbar and profiler.
inline const char* antialiasingName(AntialiasingMode mode){
    return mode == AntialiasingMode::Fxaa   ? "FXAA"
           : mode == AntialiasingMode::Msaa ? "MSAA 4x"
                                            : "Off";
}

enum class ShadingMode{ Phong, Toon, UnmappedPhong };
enum class BufferView{
    Lit,
    Position,
    Normal,
    Albedo,
    Specular,
    Depth,
    Ambient,
    Emissive,
    Plane,
    Occlusion
};
} // namespace vr
