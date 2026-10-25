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

constexpr std::array<ToolbarButton, 8> buttons{{{Action::PointRed, 78, 56},
                                                {Action::PointGreen, 142, 56},
                                                {Action::PointBlue, 206, 56},
                                                {Action::PointWhite, 270, 56},
                                                {Action::PointSun, 334, 56},
                                                {Action::ShaderMenu, 418, 168},
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

void SceneOverlay::click(Application& app, float x, float y){
    const float bottom = float(app.m_height - FooterHeight);
    if (app.m_shaderMenuOpen && x >= 418 && x <= 586){
        if (y >= bottom - 72 && y < bottom - 40){
            app.execute(Action::PhongShader);
            return;
        }
        if (y >= bottom - 40 && y < bottom - 8){
            app.execute(Action::ToonShader);
            return;
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
    m_text.rectangle(0, 0, float(app.m_width), HeaderHeight, {.025f, .04f, .065f, .96f});
    m_text.draw("Scene viewer", 24, 12, 22, {.94f, .96f, 1, 1});
    m_text.draw("HOUSE + PATIO", 190, 20, 11, {.3f, .84f, .73f, 1});
    std::ostringstream info;
    info << stats.draws << " draws  |  " << std::fixed << std::setprecision(0)
         << app.m_framesPerSecond << " FPS  |  CPU cull " << std::setprecision(2)
         << stats.cullCpuMs + app.m_portalCpuMs << " ms";
    m_text.draw(info.str(), float(app.m_width) - 400, 20, 12, {.7f, .8f, .88f, 1});
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
              draws ? glm::vec4(.3f, .93f, .78f, 1) : glm::vec4(1, .4f, .3f, 1));
    }
}

// Draws the available controls and current application status.
void SceneOverlay::drawFooter(const Application& app){
    const float bottom = float(app.m_height - FooterHeight), top = bottom + 12;
    m_text.rectangle(0, bottom, float(app.m_width), FooterHeight, {.025f, .04f, .065f, .97f});
    m_text.draw("Lights", 20, top + 7, 13, {.7f, .8f, .88f, 1});
    const std::array<const char*, 5> names{{"1 Red", "2 Green", "3 Blue", "4 White", "5 Sun"}};
    const std::array<glm::vec4, 5> colors{{{1, .4f, .35f, 1},
                                           {.35f, .95f, .6f, 1},
                                           {.4f, .65f, 1, 1},
                                           {.92f, .94f, 1, 1},
                                           {1, .85f, .5f, 1}}};
    for (const auto& button : buttons){
        std::string text;
        glm::vec4 foreground(.8f, .87f, .92f, 1), background(.09f, .13f, .18f, 1);
        if (const auto index = Controls::pointLightIndex(button.action)){
            const bool on = app.m_pointLights[*index];
            foreground = on ? colors[*index] : glm::vec4(.4f, .46f, .52f, 1);
            background = on ? glm::vec4(.14f, .22f, .27f, 1) : background;
            text = names[*index];
        } else if (button.action == Action::ShaderMenu){
            text = std::string("Shader: ") + shaderName(app.m_shadingMode) + " [Z]";
        } else if (button.action == Action::Culling){
            const bool enabled = app.m_frustumCulling || app.m_portalCulling;
            text = std::string("Culling: ") + (enabled ? "ON" : "OFF") + " [C]";
            if (enabled){
                foreground = {.3f, .9f, .78f, 1};
            }
        } else{
            text = app.m_frozenEye   ? "View: Observation"
                   : app.m_houseView ? "View: House"
                                     : "View: Patio";
        }
        m_text.rectangle(button.x, top, button.width, 32, background);
        m_text.draw(text, button.x + 7, top + 7, 11, foreground);
    }
    m_text.draw("RMB + WASD/QE: fly   Shift: faster   1-5: lights   Z: shader menu   C: culling   "
                "Space: reset   R: reload   Esc: close",
                20, bottom + 53, 11, {.68f, .77f, .85f, 1});
    m_text.draw(app.m_status.substr(0, 140), 20, bottom + 76, 10, {.42f, .54f, .64f, 1});
    if (app.m_shaderMenuOpen){
        for (unsigned i = 0; i < 2; ++i){
            const auto mode = i == 0 ? ShadingMode::Phong : ShadingMode::Toon;
            const float y = bottom - 72 + 32 * i;
            m_text.rectangle(418, y, 168, 32,
                             mode == app.m_shadingMode ? glm::vec4(.12f, .3f, .31f, 1)
                                                       : glm::vec4(.06f, .1f, .15f, 1));
            m_text.draw(std::to_string(i + 1) + "  " + shaderName(mode), 430, y + 7, 13,
                        {.9f, .96f, 1, 1});
        }
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
                     subtitle.empty() ? 28.0f : 49.0f, {0.025f, 0.04f, 0.065f, 0.86f});
    m_text.draw(title, x, y, 14, color);
    m_text.draw(subtitle, x, y + 20, 12, {0.65f, 0.72f, 0.8f, 1});
}

} // namespace vr
