#pragma once
#include "Text.h"

namespace vr{
class Application;

class SceneOverlay final{
public:
    static constexpr int HeaderHeight = 56;
    static constexpr int FooterHeight = 96;

    // Creates the text renderer used by the application HUD.
    explicit SceneOverlay(const std::filesystem::path& root) : m_text(root){
    }

    // Draws the complete HUD without modifying the scene.
    void draw(const Application& application);
    // Dispatches a framebuffer-coordinate click to a toolbar or shader-menu action.
    void click(Application& application, float x, float y);

private:
    // Draws measured pipeline timings, frame history and submission/culling counters.
    void drawPerformance(const Application& application);

    // Draws the application title and current rendering statistics.
    void drawHeader(const Application& application);

    // Marks the optical center used by automatic depth-of-field focusing.
    void drawFocus(const Application& application);

    // Projects the demonstration labels into the current camera view.
    void drawLabels(const Application& application);

    // Draws the available controls and current application status.
    void drawFooter(const Application& application);

    // Draws the toolbar using the same button rectangles as click handling.
    void drawToolbar(const Application& application, float top);

    // Draws the shader choices using the same rows as menu hit testing.
    void drawShaderMenu(const Application& application, float bottom);

    // Projects a world-space annotation and draws it inside the usable viewport.
    void label(const Application& application, const glm::vec3& position, const std::string& title,
               const std::string& subtitle, const glm::vec4& color);
    Text m_text;
};
} // namespace vr
