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
        Capture,
        ExportGraph
    };

    struct Binding{
        Action action;
        int key;
        const char* keyText;
        const char* label;
    };

    // Returns the immutable command table used by input handling and tests.
    static const std::array<Binding, 16>& getBindings(){
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
        if (action < Action::PointRed || action > Action::PointWhite){
            return{};
        }
        return unsigned(action) - unsigned(Action::PointRed);
    }

private:
    inline static constexpr std::array<Binding, 16> m_bindings{
        {{Action::Exit, GLFW_KEY_ESCAPE, "Esc", "Exit"},
         {Action::ResetView, GLFW_KEY_SPACE, "Space", "Reset view"},
         {Action::ReloadScene, GLFW_KEY_R, "R", "Reload scene"},
         {Action::ObjectAnimation, GLFW_KEY_P, "P", "Object transform"},
         {Action::PointOrbit, GLFW_KEY_L, "L", "Point-light orbit"},
         {Action::LodTour, GLFW_KEY_V, "V", "LOD camera tour"},
         {Action::GlobalLighting, GLFW_KEY_G, "G", "Global lighting"},
         {Action::BackFaceCulling, GLFW_KEY_B, "B", "Back-face culling"},
         {Action::GroundMultitexture, GLFW_KEY_M, "M", "Ground multitexture"},
         {Action::MonkeyToon, GLFW_KEY_T, "T", "Monkey toon"},
         {Action::PointRed, GLFW_KEY_1, "1", "Point Red"},
         {Action::PointGreen, GLFW_KEY_2, "2", "Point Green"},
         {Action::PointBlue, GLFW_KEY_3, "3", "Point Blue"},
         {Action::PointWhite, GLFW_KEY_4, "4", "Point White"},
         {Action::Capture, GLFW_KEY_F2, "F2", "Save frame"},
         {Action::ExportGraph, GLFW_KEY_F3, "F3", "Save graph"}}};
};
} // namespace vr
