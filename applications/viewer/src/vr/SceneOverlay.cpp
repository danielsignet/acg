#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vr/Application.h>
#include <vr/SceneOverlay.h>

namespace vr{
// Formats the model variant and triangle count for one cow detail level.
std::string SceneOverlay::cowDetail(std::size_t level){
    const std::array<std::pair<const char*, unsigned>, 6> levels{{
        {"0.99", 9496},
        {"0.5", 4796},
        {"0.3", 2877},
        {"0.1", 958},
        {"0.05", 478},
        {"0.01", 94},
    }};
    const auto& detail = levels.at(level);
    return std::string(detail.first) + " / " + std::to_string(detail.second) + " tris";
}

// Draws the complete HUD without modifying the scene.
void SceneOverlay::draw(const Application& application){
    m_text.begin(application.m_width, application.m_height);
    drawHeader(application);
    drawLabels(application);
    drawFooter(application);
    m_text.end();
}

// Draws the application title and current rendering statistics.
void SceneOverlay::drawHeader(const Application& application){

    const auto& stats = application.m_renderer->getStats();
    m_text.rectangle(0, 0, static_cast<float>(application.m_width),
                     static_cast<float>(HeaderHeight),{0.025f, 0.04f, 0.065f, 0.94f});
    m_text.rectangle(28, 26, 4, 52,{0.23f, 0.83f, 0.71f, 1});
    m_text.draw("INTERACTIVE 3D VIEWER", 46, 18, 13,{0.52f, 0.64f, 0.75f, 1});
    m_text.draw("Scene graph", 44, 38, 32,{0.94f, 0.96f, 1, 1});
    m_text.draw("HIERARCHY, STATE & INSTANCING", 46, 80, 11,{0.55f, 0.68f, 0.78f, 1});
    std::ostringstream info;
    info << stats.draws << " draw calls  /  " << stats.triangles << " triangles  /  " << std::fixed
         << std::setprecision(0) << application.m_framesPerSecond << " fps";
    m_text.draw("OPENGL 4.1 CORE", static_cast<float>(application.m_width) - 335, 31, 16,
                {0.7f, 0.85f, 0.94f, 1});
    m_text.draw(info.str(), static_cast<float>(application.m_width) - 335, 60, 12,
                {0.52f, 0.64f, 0.75f, 1});
    const auto cowLod = stats.lodLevels.find("cowLOD");
    const auto level = cowLod == stats.lodLevels.end() ? 0 : cowLod->second;
    const auto detail = cowDetail(level);
    m_text.draw("COW LOD " + std::to_string(level) + "  /  " + detail,
                static_cast<float>(application.m_width) - 335, 82, 12,{1, 0.86f, 0.63f, 1});
}

// Projects the demonstration labels into the current camera view.
void SceneOverlay::drawLabels(const Application& application){
    const auto& stats = application.m_renderer->getStats();
    const auto cowLod = stats.lodLevels.find("cowLOD");
    const auto level = cowLod == stats.lodLevels.end() ? 0 : cowLod->second;
    const auto detail = cowDetail(level);
    label(application,{-6, 0.8f, 2.5f}, "01  SHARED GEOMETRY", "One model, three placements",
          {0.3f, 0.93f, 0.78f, 1});
    label(application,{0, 0.8f, 2.5f}, "02  OBJECT TRANSFORM",
          application.m_animationEnabled ? "RUNNING  [P] pause" : "PAUSED  [P] resume",
          {1, 0.72f, 0.32f, 1});
    label(application,{6, 0.8f, 2.5f}, "03  COW LOD", detail + "  [V] tour",{1, 0.86f, 0.63f, 1});
    label(application,{12, 0.8f, 2.5f}, "04  TOON SHADING",
          application.m_toonEnabled ? "ON  [T] disable" : "OFF  [T] enable",{0.45f, 0.65f, 1, 1});
}

// Draws the available controls and current application status.
void SceneOverlay::drawFooter(const Application& application){
    const float bottom = static_cast<float>(application.m_height - FooterHeight);
    m_text.rectangle(0, bottom, static_cast<float>(application.m_width),
                     static_cast<float>(FooterHeight),{0.025f, 0.04f, 0.065f, 0.94f});
    using Action = Controls::Action;
    const auto on = [](bool value){ return value ? ": ON" : ": OFF"; };
    const auto motion = [](bool value){ return value ? ": RUNNING" : ": PAUSED"; };
    const auto caption = [](Action action){ return Controls::caption(action); };
    const auto textColor = glm::vec4(0.7f, 0.78f, 0.85f, 1);
    m_text.draw("CAMERA   WASD move   Q/E down/up   Right mouse look   Shift fast   " +
                    caption(Action::ResetView) + "   " + caption(Action::ReloadScene) + "   " +
                    caption(Action::Exit),
                28, bottom + 12, 13,{0.83f, 0.88f, 0.93f, 1});
    const auto state = application.m_scene->getRoot()->getState();
    std::string lighting = caption(Action::GlobalLighting) + on(application.m_lightingEnabled);
    for (const auto action :
         {Action::PointRed, Action::PointGreen, Action::PointBlue, Action::PointWhite}){
        lighting += "   " + caption(action) +
                    on(state->getLightEnabled(*Controls::pointLightIndex(action)));
    }
    m_text.draw(lighting, 28, bottom + 38, 12,{0.3f, 0.84f, 0.73f, 1});
    m_text.draw(
        caption(Action::ObjectAnimation) + motion(application.m_animationEnabled) + "   " +
            caption(Action::PointOrbit) +
            motion(application.m_lightOrbitCallback &&
                   application.m_lightOrbitCallback->getEnabled()) +
            "   " + caption(Action::LodTour) +
            on(application.m_lodDemoCallback && application.m_lodDemoCallback->getEnabled()) +
            "   Arrows: move point-light ring",
        28, bottom + 62, 12, textColor);
    m_text.draw(caption(Action::BackFaceCulling) + on(application.m_cullFaceEnabled) + "   " +
                    caption(Action::GroundMultitexture) + on(application.m_multitextureEnabled) +
                    "   " + caption(Action::Capture),
                28, bottom + 86, 12, textColor);
    m_text.draw(caption(Action::ExportGraph) + "   " + application.m_status.substr(0, 130), 28,
                bottom + 116, 11,{0.43f, 0.53f, 0.63f, 1});
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
    m_text.rectangle(x - 10, y - 4, 200, 49,{0.025f, 0.04f, 0.065f, 0.86f});
    m_text.draw(title, x, y, 14, color);
    m_text.draw(subtitle, x, y + 20, 12,{0.65f, 0.72f, 0.8f, 1});
}

} // namespace vr
