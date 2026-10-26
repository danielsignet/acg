#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vr/Application.h>
#include <vr/SceneOverlay.h>

namespace vr{
namespace{
// Formats measured milliseconds and memory without unstable decimal widths.
std::string decimal(double value, int precision = 2){
    std::ostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}

// Groups large submission counts for scanning at a glance.
std::string count(std::size_t value){
    auto text = std::to_string(value);
    for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3){
        text.insert(static_cast<std::size_t>(i), ",");
    }
    return text;
}
} // namespace

// Draws measured pipeline timings, frame history and submission/culling counters.
void SceneOverlay::drawPerformance(const Application& app){
    const auto& profiler = app.m_profiler;
    const auto timings = profiler.summarize();
    const auto& scene = app.m_renderer->getStats();
    const auto& shadows = app.m_shadowRenderer->getStats();
    const auto& portal = app.m_portalResult;
    const float width = static_cast<float>(app.m_width);
    const float top = HeaderHeight + 12.0f;
    const float bottom = static_cast<float>(app.m_height - FooterHeight - 8);
    const float middle = width * 0.5f;
    const float row = std::clamp((bottom - top - 142) / FrameProfiler::StageCount, 16.0f, 24.0f);
    const float font = row < 20 ? 12.0f : 14.0f;
    const glm::vec4 foreground(0.85f, 0.91f, 0.95f, 1), muted(0.52f, 0.66f, 0.74f, 1);
    const glm::vec4 cyan(0.32f, 0.9f, 0.8f, 1), gold(1, 0.77f, 0.4f, 1);
    m_text.rectangle(16, top, width - 32, bottom - top,{0.025f, 0.045f, 0.065f, 0.97f});
    m_text.draw("PERFORMANCE", 32, top + 8, 20, foreground);
    m_text.draw("F1 / Esc: close", width - 155, top + 12, 12, muted);
    const auto fps = timings.frameMs > 0 ? 1000 / timings.frameMs : 0;
    m_text.draw(decimal(fps, 1) + " FPS   |   " + decimal(timings.frameMs) +
                    " ms/frame   |   P95 " + decimal(timings.p95Ms) + " ms   |   " +
                    std::to_string(app.m_width) + " x " + std::to_string(app.m_height) +
                    "   |   VSync " + (app.m_vsync ? "ON" : "OFF"),
                32, top + 37, 14, cyan);

    m_text.draw("PIPELINE", 32, top + 66, 12, muted);
    m_text.draw("CPU ms", middle - 165, top + 66, 12, cyan);
    m_text.draw("GPU ms", middle - 82, top + 66, 12, gold);
    const auto& descriptions = FrameProfiler::descriptions();
    const auto renderStage = static_cast<std::size_t>(FrameProfiler::Stage::Render);
    for (std::size_t i = 0; i < descriptions.size(); ++i){
        const float y = top + 86 + static_cast<float>(i) * row;
        const bool aggregate = i == 0 || i == renderStage;
        if (aggregate){
            m_text.rectangle(26, y - 2, middle - 42, row,{0.065f, 0.105f, 0.14f, 1});
        }
        const auto& description = descriptions[i];
        m_text.draw(description.name, 32, y, font, aggregate ? foreground : muted);
        m_text.draw(timings.samples ? decimal(timings.cpu[i]) : "...", middle - 165, y, font, cyan);
        const auto gpu = !description.gpu              ? "-"
                         : !profiler.getGpuSupported() ? "N/A"
                         : !timings.gpuSamples         ? "..."
                                                       : decimal(timings.gpu[i]);
        m_text.draw(gpu, middle - 82, y, font, gold);
    }

    m_text.draw("SCENE / CULLING  (current frame)", middle + 16, top + 66, 12, muted);
    float y = top + 86;
    const auto stat = [&](const std::string& name, const std::string& value){
        m_text.draw(name, middle + 16, y, font, muted);
        m_text.draw(value, width - 180, y, font, foreground);
        y += row;
    };
    stat("Color draws / transparent", count(scene.draws) + " / " + count(scene.transparentDraws));
    stat("Color triangles", count(scene.triangles));
    stat("Shadow draws / passes", count(shadows.draws) + " / " + count(shadows.passes));
    stat("Shadow triangles", count(shadows.triangles));
    stat("Scene draws, all passes", count(scene.draws + shadows.draws));
    stat("Scene triangles, all passes", count(scene.triangles + shadows.triangles));
    stat("Screen-quad draws", count(app.m_deferred->getScreenDraws()));
    stat("Active lights", count(scene.lights));
    stat("Visited scene nodes", count(scene.visitedNodes));
    stat("Camera bounds tested", count(scene.boundsTested));
    stat("Frustum / portal subtrees cut",
         count(scene.subtreesRejected) + " / " + count(scene.portalRejected));
    stat("Shadow bounds tested / cut",
         count(shadows.boundsTested) + " / " + count(shadows.rejected));
    const auto& cells = app.m_scene->getPortalGraph().getCells();
    const auto rooms = std::count_if(cells.begin(), cells.end(),
                                     [&](const auto& cell){ return portal.visible(cell.id); });
    stat("Rooms visible / portals tested",
         count(rooms) + "/" + count(cells.size()) + " / " + count(portal.portalsTested));
    std::string levels;
    for (const auto& entry : scene.lodLevels){
        if (!levels.empty()){
            levels += ", ";
        }
        levels += std::to_string(entry.second);
    }
    stat("Selected LOD indices", levels.empty() ? "-" : levels.substr(0, 20));
    const double gbufferMiB = static_cast<double>(app.m_width) * app.m_height * 64 / (1024 * 1024);
    stat("G-buffer + depth (allocated)", decimal(gbufferMiB, 1) + " MiB");
    stat("Frustum / portals / frozen", std::string(app.m_frustumCulling ? "ON" : "OFF") + " / " +
                                           (app.m_portalCulling ? "ON" : "OFF") + " / " +
                                           (app.m_frozenEye ? "YES" : "NO"));
    stat("Portal origin", portal.origin.substr(0, 20));

    // Keep the chart legible even at the minimum supported window size.
    const float chartTop = top + 86 + FrameProfiler::StageCount * row + 5;
    const float chartHeight = std::max(8.0f, bottom - chartTop - 39);
    const float chartWidth = middle - 48;
    const float limit = std::max(33.34f, static_cast<float>(timings.p95Ms * 1.2));
    m_text.rectangle(32, chartTop, chartWidth, chartHeight,{0.04f, 0.075f, 0.1f, 1});
    const float barWidth = chartWidth / FrameProfiler::HistorySize;
    std::vector<glm::vec4> fast, slow;
    std::size_t index = 0;
    for (const auto& sample : profiler.getHistory()){
        const float height =
            std::min(chartHeight, static_cast<float>(sample.intervalMs) / limit * chartHeight);
        (sample.intervalMs > 16.67 ? slow : fast)
            .push_back({32 + static_cast<float>(index++) * barWidth,
                        chartTop + chartHeight - height, std::max(1.0f, barWidth - 1), height});
    }
    m_text.rectangles(fast, cyan);
    m_text.rectangles(slow, gold);
    const float budget = chartTop + chartHeight * (1 - 16.67f / limit);
    m_text.rectangle(32, budget, chartWidth, 1,{0.4f, 0.55f, 0.65f, 0.8f});
    m_text.draw("120 frames | line: 16.67 ms (60 FPS)", 34, bottom - 35, 10, muted);
    m_text.draw("GPU delay: " + std::to_string(timings.gpuLag) + " frames | skipped: " +
                    std::to_string(timings.skipped) + " | avg: " + std::to_string(timings.samples),
                middle + 16, bottom - 35, 10, muted);
    m_text.draw("Nested totals overlap. GPU is elapsed time; CPU includes driver work. Triangles "
                "are submitted, before GPU clipping; scene counts exclude HUD/debug/quads.",
                32, bottom - 17, 10, muted);
}
} // namespace vr
