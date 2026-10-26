#pragma once

namespace vr{
enum class ShadingMode{ Phong, Toon };
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
