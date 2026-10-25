#pragma once
#include "Text.h"

namespace vr{
class Application;

class SceneOverlay final{
public:
    static constexpr int HeaderHeight = 106;
    static constexpr int FooterHeight = 160;

    // Creates the text renderer used by the application HUD.
    explicit SceneOverlay(const std::filesystem::path& root) : m_text(root){
    }

    // Draws the complete HUD without modifying the scene.
    void draw(const Application& application);

private:
    // Formats the model variant and triangle count for one cow detail level.
    static std::string cowDetail(std::size_t level);

    // Draws the application title and current rendering statistics.
    void drawHeader(const Application& application);

    // Projects the demonstration labels into the current camera view.
    void drawLabels(const Application& application);

    // Draws the available controls and current application status.
    void drawFooter(const Application& application);

    // Projects a world-space annotation and draws it inside the usable viewport.
    void label(const Application& application, const glm::vec3& position, const std::string& title,
               const std::string& subtitle, const glm::vec4& color);
    Text m_text;
};
} // namespace vr
