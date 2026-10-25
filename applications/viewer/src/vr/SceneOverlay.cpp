#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vr/Application.h>
#include <vr/SceneOverlay.h>

namespace vr{
namespace{
using Action = Controls::Action;

struct ToolbarButton{
    Action action;
    float x, width;
};

constexpr ToolbarButton shaderButton{Action::ShaderMenu, 418, 168};
constexpr ToolbarButton shadowButton{Action::Shadows, 348, 148};
constexpr float shaderRowHeight = 32;
constexpr float shaderMenuTop = -72;

struct ShaderOption{
    Action action;
    ShadingMode mode;
    const char* caption;
};

constexpr std::array<ShaderOption, 2> shaderOptions{
    {{Action::PhongShader, ShadingMode::Phong, "1  Phong"},
     {Action::ToonShader, ShadingMode::Toon, "2  Toon"}}};

constexpr std::array<ToolbarButton, 8> buttons{{{Action::PointRed, 78, 56},
                                                {Action::PointGreen, 142, 56},
                                                {Action::PointBlue, 206, 56},
                                                {Action::PointWhite, 270, 56},
                                                {Action::PointSun, 334, 56},
                                                shaderButton,
                                                {Action::Culling, 602, 120},
                                                {Action::HouseView, 738, 180}}};

const char* shaderName(ShadingMode mode){
    return mode == ShadingMode::Phong ? "Phong" : "Toon";
}
} // namespace

// Draws the complete HUD without modifying the scene.
void SceneOverlay::draw(const Application& application){
    m_text.begin(application.m_width, application.m_height);
    drawHeader(application);
    drawLabels(application);
    drawFooter(application);
    m_text.end();
}

// Dispatches a framebuffer-coordinate click to a toolbar or shader-menu action.
void SceneOverlay::click(Application& app, float x, float y){
    if (x >= shadowButton.x && x <= shadowButton.x + shadowButton.width && y >= 12 && y <= 44){
        app.execute(shadowButton.action);
        return;
    }
    const float bottom = static_cast<float>(app.m_height - FooterHeight);
    if (app.m_shaderMenuOpen && x >= shaderButton.x && x <= shaderButton.x + shaderButton.width){
        for (std::size_t index = 0; index < shaderOptions.size(); ++index){
            const float top = bottom + shaderMenuTop + shaderRowHeight * static_cast<float>(index);
            if (y >= top && y < top + shaderRowHeight){
                app.execute(shaderOptions[index].action);
                return;
            }
        }
    }
    if (y >= bottom + 12 && y <= bottom + 44){
        for (const auto& button : buttons){
            if (x >= button.x && x <= button.x + button.width){
                if (button.action != Action::ShaderMenu){
                    app.m_shaderMenuOpen = false;
                }
                app.execute(button.action);
                return;
            }
        }
    }
    app.m_shaderMenuOpen = false;
}

// Reuses the existing overlay renderer with a compact viewport header.
void SceneOverlay::drawHeader(const Application& app){
    const auto& stats = app.m_renderer->getStats();
    m_text.rectangle(0, 0, static_cast<float>(app.m_width), HeaderHeight,
                     {0.025f, 0.04f, 0.065f, 0.96f});
    m_text.draw("ACG", 24, 12, 22,{0.94f, 0.96f, 1, 1});
    m_text.draw("HOUSE + PATIO", 190, 20, 11,{0.3f, 0.84f, 0.73f, 1});
    m_text.rectangle(shadowButton.x, 12, shadowButton.width, 32,{0.09f, 0.13f, 0.18f, 1});
    m_text.draw(std::string("Shadows: ") + (app.m_shadowsEnabled ? "ON" : "OFF") + " [J]",
                shadowButton.x + 10, 21, 12,{0.7f, 0.9f, 0.84f, 1});
    std::ostringstream info;
    info << stats.draws << " draws  |  " << std::fixed << std::setprecision(0)
         << app.m_framesPerSecond << " FPS  |  CPU cull " << std::setprecision(2)
         << stats.cullCpuMs + app.m_portalCpuMs << " ms";
    m_text.draw(info.str(), static_cast<float>(app.m_width) - 400, 20, 12,{0.7f, 0.8f, 0.88f, 1});
}

// Projects the demonstration labels into the current camera view.
void SceneOverlay::drawLabels(const Application& app){
    if (!app.m_frozenEye || app.m_frustumCulling){
        return;
    }
    const auto& stats = app.m_renderer->getStats();
    for (const auto& cell : app.m_scene->getPortalGraph().getCells()){
        const auto found = stats.roomDraws.find(cell.id);
        const auto draws = found == stats.roomDraws.end() ? 0 : found->second;
        label(app, cell.bounds.getCenter(), "ROOM " + cell.id + " / " + std::to_string(draws), "",
              draws ? glm::vec4(0.3f, 0.93f, 0.78f, 1) : glm::vec4(1, 0.4f, 0.3f, 1));
    }
}

// Draws the available controls and current application status.
void SceneOverlay::drawFooter(const Application& app){
    const float bottom = static_cast<float>(app.m_height - FooterHeight), top = bottom + 12;
    m_text.rectangle(0, bottom, static_cast<float>(app.m_width), FooterHeight,
                     {0.025f, 0.04f, 0.065f, 0.97f});
    m_text.draw("Lights", 20, top + 7, 13,{0.7f, 0.8f, 0.88f, 1});
    drawToolbar(app, top);
    m_text.draw(
        "RMB+WASD/QE: fly   Shift: faster   Arrows/PgUp/PgDn: move lamps   F: freeze view   "
        "Space: reset   R: reload   Esc: close",
        20, bottom + 53, 11,{0.68f, 0.77f, 0.85f, 1});
    const auto& shadowStats = app.m_shadowRenderer->getStats();
    const auto status = app.m_status + " | Shadow passes: " + std::to_string(shadowStats.passes) +
                        ", draws: " + std::to_string(shadowStats.draws);
    m_text.draw(status.substr(0, 160), 20, bottom + 76, 10,{0.42f, 0.54f, 0.64f, 1});
    if (app.m_shaderMenuOpen){
        drawShaderMenu(app, bottom);
    }
}

// Draws the toolbar using the same button rectangles as click handling.
void SceneOverlay::drawToolbar(const Application& app, float top){
    const std::array<const char*, 5> names{{"1 Red", "2 Green", "3 Blue", "4 White", "5 Sun"}};
    const std::array<glm::vec4, 5> colors{{{1, 0.4f, 0.35f, 1},
                                           {0.35f, 0.95f, 0.6f, 1},
                                           {0.4f, 0.65f, 1, 1},
                                           {0.92f, 0.94f, 1, 1},
                                           {1, 0.85f, 0.5f, 1}}};
    for (const auto& button : buttons){
        std::string text;
        glm::vec4 foreground(0.8f, 0.87f, 0.92f, 1), background(0.09f, 0.13f, 0.18f, 1);
        if (const auto index = Controls::pointLightIndex(button.action)){
            const bool on = app.m_pointLights[*index];
            foreground = on ? colors[*index] : glm::vec4(0.4f, 0.46f, 0.52f, 1);
            background = on ? glm::vec4(0.14f, 0.22f, 0.27f, 1) : background;
            text = names[*index];
        } else if (button.action == Action::ShaderMenu){
            text = std::string("Shader: ") + shaderName(app.m_shadingMode) + " [Z]";
        } else if (button.action == Action::Culling){
            const bool enabled = app.m_frustumCulling || app.m_portalCulling;
            text = std::string("Culling: ") + (enabled ? "ON" : "OFF") + " [C]";
            if (enabled){
                foreground ={0.3f, 0.9f, 0.78f, 1};
            }
        } else{
            text = app.m_frozenEye   ? "View: Observation"
                   : app.m_houseView ? "View: House"
                                     : "View: Patio";
        }
        m_text.rectangle(button.x, top, button.width, 32, background);
        m_text.draw(text, button.x + 7, top + 7, 11, foreground);
    }
}

// Draws the shader choices using the same rows as menu hit testing.
void SceneOverlay::drawShaderMenu(const Application& app, float bottom){
    for (std::size_t index = 0; index < shaderOptions.size(); ++index){
        const auto& option = shaderOptions[index];
        const float top = bottom + shaderMenuTop + shaderRowHeight * static_cast<float>(index);
        m_text.rectangle(shaderButton.x, top, shaderButton.width, shaderRowHeight,
                         option.mode == app.m_shadingMode ? glm::vec4(0.12f, 0.3f, 0.31f, 1)
                                                          : glm::vec4(0.06f, 0.1f, 0.15f, 1));
        m_text.draw(option.caption, shaderButton.x + 12, top + 7, 13,{0.9f, 0.96f, 1, 1});
    }
}

// Projects a world-space annotation and draws it inside the usable viewport.
void SceneOverlay::label(const Application& application, const glm::vec3& position,
                         const std::string& title, const std::string& subtitle,
                         const glm::vec4& color){

    const auto p = application.m_scene->getCamera()->getProjection() *
                   application.m_scene->getCamera()->getView() * glm::vec4(position, 1);
    if (p.w <= 0){
        return;
    }
    const auto ndc = glm::vec3(p) / p.w;
    if (std::abs(ndc.x) > 0.95f || ndc.y < -0.68f || ndc.y > 0.6f){
        return;
    }
    const float x = std::clamp((ndc.x * 0.5f + 0.5f) * application.m_width - 85, 18.0f,
                               std::max(18.0f, static_cast<float>(application.m_width) - 208));
    const float y = std::clamp(
        (0.5f - ndc.y * 0.5f) * application.m_height, static_cast<float>(HeaderHeight + 10),
        std::max(static_cast<float>(HeaderHeight + 10),
                 static_cast<float>(application.m_height - FooterHeight - 54)));
    m_text.rectangle(x - 10, y - 4, subtitle.empty() ? 128.0f : 200.0f,
                     subtitle.empty() ? 28.0f : 49.0f,{0.025f, 0.04f, 0.065f, 0.86f});
    m_text.draw(title, x, y, 14, color);
    m_text.draw(subtitle, x, y + 20, 12,{0.65f, 0.72f, 0.8f, 1});
}

} // namespace vr
