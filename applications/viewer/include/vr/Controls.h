#pragma once
#include "RenderMode.h"
#include <GLFW/glfw3.h>
#include <array>
#include <optional>
#include <string>

namespace vr{
class Controls final{
public:
    enum class Action{
        Exit,
        ResetView,
        ReloadScene,
        ObjectAnimation,
        PointOrbit,
        LodTour,
        GlobalLighting,
        BackFaceCulling,
        GroundMultitexture,
        MonkeyToon,
        PointRed,
        PointGreen,
        PointBlue,
        PointWhite,
        PointSun,
        Capture,
        ExportGraph,
        FrustumCulling,
        FreezeFrustum,
        DebugBounds,
        Roof,
        Hud,
        Performance,
        ResetAverages,
        HouseView,
        PortalCulling,
        TransparentWalls,
        CullingDemo,
        Culling,
        ShaderMenu,
        PhongShader,
        ToonShader,
        Shadows,
        Flashlight,
        Bloom,
        DepthOfField,
        BufferPreviews,
        PositionBuffer,
        NormalBuffer,
        AlbedoBuffer,
        SpecularBuffer,
        DepthBuffer,
        AmbientBuffer,
        EmissiveBuffer,
        PlaneBuffer
    };

    struct Binding{
        Action action;
        int key;
        const char* keyText;
        const char* label;
    };

    struct ShaderOption{
        Action action;
        ShadingMode mode;
        BufferView view;
        int key;
        const char* caption;
    };

    // Shares shader and buffer choices between menu rendering and keyboard selection.
    static constexpr const std::array<ShaderOption, 10>& getShaderOptions(){
        return m_shaderOptions;
    }

    // Finds the menu choice assigned to a key without affecting viewport light bindings.
    static const ShaderOption* findShader(int key){
        for (const auto& option : m_shaderOptions){
            if (option.key == key){
                return &option;
            }
        }
        return nullptr;
    }

    // Returns the human-readable name of a displayed surface attachment.
    static const char* bufferName(BufferView view){
        constexpr std::array<const char*, 9> names{{"Lit", "Positions", "Normals", "Albedo",
                                                    "Specular", "Depth", "Ambient", "Emissive",
                                                    "Geom normals"}};
        return names.at(static_cast<unsigned>(view));
    }

    // Returns the immutable command table used by input handling and tests.
    static const std::array<Binding, 17>& getBindings(){
        return m_bindings;
    }

    // Finds the command bound to a key, or returns no binding.
    static const Binding* find(int key){
        for (const auto& binding : m_bindings){
            if (binding.key == key){
                return &binding;
            }
        }
        return nullptr;
    }

    // Formats the label and key shown for a command in the HUD.
    static std::string caption(Action action){
        for (const auto& binding : m_bindings){
            if (binding.action == action){
                return std::string(binding.label) + " [" + binding.keyText + "]";
            }
        }
        return{};
    }

    // Maps individual point-light commands to their state slots.
    static std::optional<unsigned> pointLightIndex(Action action){
        if (action < Action::PointRed || action > Action::PointSun){
            return{};
        }
        return static_cast<unsigned>(action) - static_cast<unsigned>(Action::PointRed);
    }

private:
    inline static constexpr std::array<ShaderOption, 10> m_shaderOptions{
        {{Action::PhongShader, ShadingMode::Phong, BufferView::Lit, GLFW_KEY_1, "1  Phong"},
         {Action::ToonShader, ShadingMode::Toon, BufferView::Lit, GLFW_KEY_2, "2  Toon"},
         {Action::PositionBuffer, ShadingMode::Phong, BufferView::Position, GLFW_KEY_3,
          "3  Positions"},
         {Action::NormalBuffer, ShadingMode::Phong, BufferView::Normal, GLFW_KEY_4, "4  Normals"},
         {Action::AlbedoBuffer, ShadingMode::Phong, BufferView::Albedo, GLFW_KEY_5, "5  Albedo"},
         {Action::SpecularBuffer, ShadingMode::Phong, BufferView::Specular, GLFW_KEY_6,
          "6  Specular"},
         {Action::DepthBuffer, ShadingMode::Phong, BufferView::Depth, GLFW_KEY_7, "7  Depth"},
         {Action::AmbientBuffer, ShadingMode::Phong, BufferView::Ambient, GLFW_KEY_8, "8  Ambient"},
         {Action::EmissiveBuffer, ShadingMode::Phong, BufferView::Emissive, GLFW_KEY_9,
          "9  Emissive"},
         {Action::PlaneBuffer, ShadingMode::Phong, BufferView::Plane, GLFW_KEY_0,
          "0  Geom normals"}}};

    inline static constexpr std::array<Binding, 17> m_bindings{
        {{Action::Performance, GLFW_KEY_F1, "F1", "Performance"},
         {Action::Exit, GLFW_KEY_ESCAPE, "Esc", "Exit"},
         {Action::ResetView, GLFW_KEY_SPACE, "Space", "Reset view"},
         {Action::ReloadScene, GLFW_KEY_R, "R", "Reload scene"},
         {Action::PointRed, GLFW_KEY_1, "1", "Point Red"},
         {Action::PointGreen, GLFW_KEY_2, "2", "Point Green"},
         {Action::PointBlue, GLFW_KEY_3, "3", "Point Blue"},
         {Action::PointWhite, GLFW_KEY_4, "4", "Point White"},
         {Action::PointSun, GLFW_KEY_5, "5", "Sun"},
         {Action::Culling, GLFW_KEY_C, "C", "Culling"},
         {Action::FreezeFrustum, GLFW_KEY_F, "F", "Freeze view"},
         {Action::ShaderMenu, GLFW_KEY_Z, "Z", "Shader"},
         {Action::Shadows, GLFW_KEY_J, "J", "Shadows"},
         {Action::Flashlight, GLFW_KEY_L, "L", "Flashlight"},
         {Action::Bloom, GLFW_KEY_B, "B", "Bloom"},
         {Action::DepthOfField, GLFW_KEY_V, "V", "Depth of field"},
         {Action::BufferPreviews, GLFW_KEY_G, "G", "G-buffer previews"}}};
};
} // namespace vr
