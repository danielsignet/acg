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

// Groups whole and fractional counts using the same formatting in both columns.
std::string count(double value, int precision = 0){
    auto text = decimal(value, precision);
    const auto point = text.find('.');
    const auto digits = point == std::string::npos ? text.size() : point;
    for (int i = static_cast<int>(digits) - 3; i > 0; i -= 3){
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
    const float width = static_cast<float>(app.m_width);
    const float top = HeaderHeight + 12.0f;
    const float bottom = static_cast<float>(app.m_height - FooterHeight - 8);
    const float middle = width * 0.5f;
    const float row = std::clamp((bottom - top - 142) / FrameProfiler::StageCount, 16.0f, 24.0f);
    const float font = row < 20 ? 12.0f : 14.0f;
    const glm::vec4 foreground(0.95f, 0.98f, 1, 1), muted(0.8f, 0.86f, 0.92f, 1);
    const glm::vec4 cyan(0.32f, 1, 0.85f, 1), gold(1, 0.85f, 0.5f, 1);
    const auto text = [&](const std::string& value, float x, float y, float size,
                          const glm::vec4& color){
        m_text.draw(value, x + 1, y + 1, size,{0, 0, 0, 0.96f});
        m_text.draw(value, x, y, size, color);
    };
    m_text.rectangle(16, top, width - 32, bottom - top,{0.025f, 0.045f, 0.065f, 0.28f});
    text("PERFORMANCE", 32, top + 8, 20, foreground);
    text("F1 / Esc: close", width - 155, top + 12, 12, muted);
    const auto fps = timings.frameMs > 0 ? 1000 / timings.frameMs : 0;
    const auto currentMs =
        profiler.getHistory().empty() ? 0 : profiler.getHistory().back().intervalMs;
    const auto currentFps = currentMs > 0 ? 1000 / currentMs : 0;
    text("FPS " + decimal(currentFps, 1) + " / avg " + decimal(fps, 1) + "   |   frame " +
             decimal(currentMs) + " / avg " + decimal(timings.frameMs) + " ms   |   P95 " +
             decimal(timings.p95Ms) + " ms   |   " + std::to_string(app.m_width) + " x " +
             std::to_string(app.m_height) + "   |   VSync " + (app.m_vsync ? "ON" : "OFF"),
         32, top + 37, 12, cyan);

    text("PIPELINE", 32, top + 66, 12, muted);
    text("CPU ms: now / avg", middle - 226, top + 66, 10, cyan);
    text("GPU ms: now / avg", middle - 112, top + 66, 10, gold);
    const auto& descriptions = FrameProfiler::descriptions();
    const auto renderStage = static_cast<std::size_t>(FrameProfiler::Stage::Render);
    for (std::size_t i = 0; i < descriptions.size(); ++i){
        const float y = top + 86 + static_cast<float>(i) * row;
        const bool aggregate = i == 0 || i == renderStage;
        if (aggregate){
            m_text.rectangle(26, y - 2, middle - 42, row,{0.065f, 0.105f, 0.14f, 0.25f});
        }
        const auto& description = descriptions[i];
        text(description.name, 32, y, font, aggregate ? foreground : muted);
        text(timings.samples ? decimal(timings.latestCpu[i]) + " / " + decimal(timings.cpu[i])
                             : "...",
             middle - 226, y, font, cyan);
        const auto gpu = !description.gpu              ? "-"
                         : !profiler.getGpuSupported() ? "N/A"
                         : !timings.gpuSamples
                             ? "..."
                             : decimal(timings.latestGpu[i]) + " / " + decimal(timings.gpu[i]);
        text(gpu, middle - 112, y, font, gold);
    }

    text("SCENE / CULLING", middle + 16, top + 66, 12, muted);
    text("Now", width - 192, top + 66, 12, foreground);
    text("Avg", width - 99, top + 66, 12, cyan);
    const float counterRow =
        std::clamp((bottom - top - 168) / FrameProfiler::CounterCount, 13.0f, 22.0f);
    const float counterFont = counterRow < 20 ? 11.0f : 13.0f;
    const auto& counters = profiler.getCounters();
    const auto& names = FrameProfiler::counterNames();
    for (std::size_t i = 0; i < names.size(); ++i){
        const float y = top + 86 + static_cast<float>(i) * counterRow;
        const int precision =
            i == static_cast<std::size_t>(FrameProfiler::Counter::GBufferMiB) ? 1 : 0;
        text(names[i], middle + 16, y, counterFont, muted);
        text(count(counters[i], precision), width - 192, y, counterFont, foreground);
        text(timings.counterSamples ? count(timings.counters[i], 1) : "...", width - 99, y,
             counterFont, cyan);
    }
    const float stateTop = top + 92 + FrameProfiler::CounterCount * counterRow;
    text(std::string("Frustum ") + (app.m_frustumCulling ? "ON" : "OFF") + " | Portals " +
             (app.m_portalCulling ? "ON" : "OFF") + " | Frozen " + (app.m_frozenEye ? "YES" : "NO"),
         middle + 16, stateTop, 10, muted);
    std::string levels;
    for (const auto& entry : scene.lodLevels){
        if (!levels.empty()){
            levels += ", ";
        }
        levels += std::to_string(entry.second);
    }
    text("Origin: " + app.m_portalResult.origin.substr(0, 20) +
             " | Rooms: " + std::to_string(app.m_scene->getPortalGraph().getCells().size()) +
             " | LOD: " + (levels.empty() ? "-" : levels.substr(0, 20)),
         middle + 16, stateTop + 14, 10, muted);

    // Keep the chart legible even at the minimum supported window size.
    const float chartTop = top + 86 + FrameProfiler::StageCount * row + 5;
    const float chartHeight = std::max(8.0f, bottom - chartTop - 39);
    const float chartWidth = middle - 48;
    const float limit = std::max(33.34f, static_cast<float>(timings.p95Ms * 1.2));
    m_text.rectangle(32, chartTop, chartWidth, chartHeight,{0.04f, 0.075f, 0.1f, 0.25f});
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
    text("120 frames | line: 16.67 ms (60 FPS)", 34, bottom - 35, 10, muted);
    text("GPU delay: " + std::to_string(timings.gpuLag) +
             " frames | skipped: " + std::to_string(timings.skipped) +
             " | avg: " + std::to_string(timings.samples) + "/120 frames",
         middle + 16, bottom - 35, 10, muted);
    text("Nested totals overlap. GPU is elapsed time; CPU includes driver work. Triangles "
         "are submitted, before GPU clipping; scene counts exclude HUD/debug/quads.",
         32, bottom - 17, 10, muted);
}
} // namespace vr
