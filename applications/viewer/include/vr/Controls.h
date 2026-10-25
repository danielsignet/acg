#pragma once
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
        HouseView,
        PortalCulling,
        TransparentWalls,
        CullingDemo,
        Culling,
        ShaderMenu,
        PhongShader,
        ToonShader
    };

    struct Binding{
        Action action;
        int key;
        const char* keyText;
        const char* label;
    };

    // Returns the immutable command table used by input handling and tests.
    static const std::array<Binding, 10>& getBindings(){
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
    inline static constexpr std::array<Binding, 10> m_bindings{
        {{Action::Exit, GLFW_KEY_ESCAPE, "Esc", "Exit"},
         {Action::ResetView, GLFW_KEY_SPACE, "Space", "Reset view"},
         {Action::ReloadScene, GLFW_KEY_R, "R", "Reload scene"},
         {Action::PointRed, GLFW_KEY_1, "1", "Point Red"},
         {Action::PointGreen, GLFW_KEY_2, "2", "Point Green"},
         {Action::PointBlue, GLFW_KEY_3, "3", "Point Blue"},
         {Action::PointWhite, GLFW_KEY_4, "4", "Point White"},
         {Action::PointSun, GLFW_KEY_5, "5", "Sun"},
         {Action::Culling, GLFW_KEY_C, "C", "Culling"},
         {Action::ShaderMenu, GLFW_KEY_Z, "Z", "Shader"}}};
};
} // namespace vr
