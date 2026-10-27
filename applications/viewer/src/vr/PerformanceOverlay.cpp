#include <vr/Application.h>
#include <vr/SceneOverlay.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr{
namespace{
constexpr float panelWidth = 584;
constexpr float panelHeight = 416;
const std::array<glm::vec4, FrameProfiler::SliceCount> colors{{{1, 0.72f, 0.3f, 1},
                                                               {0.76f, 0.6f, 1, 1},
                                                               {0.3f, 0.95f, 0.8f, 1},
                                                               {0.4f, 0.75f, 1, 1},
                                                               {1, 0.55f, 0.7f, 1},
                                                               {0.65f, 0.9f, 0.4f, 1},
                                                               {0.65f, 0.72f, 0.8f, 1},
                                                               {1, 0.5f, 0.2f, 1},
                                                               {0.9f, 0.92f, 0.95f, 1}}};

// Formats the few timing and percentage values shown in the compact view.
std::string decimal(double value, int precision = 2){
    std::ostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}

// Keeps scene counts short enough to compare without distracting from timings.
std::string compact(double value){
    if (value >= 1e6){
        return decimal(value / 1e6, 2) + "M";
    }
    if (value >= 1000){
        return decimal(value / 1000, 1) + "k";
    }
    return decimal(value, 0);
}
} // namespace

// Shares the reset button bounds between drawing and click handling.
glm::vec4 SceneOverlay::performanceResetRectangle(){
    return{16 + panelWidth - 164, HeaderHeight + 20, 148, 28};
}

// Draws a compact CPU/GPU percentage breakdown and essential scene counters.
void SceneOverlay::drawPerformance(const Application& app){
    const auto& profiler = app.m_profiler;
    const auto summary = profiler.summarize();
    const auto slices = FrameProfiler::breakdown(summary);
    const float top = HeaderHeight + 12.0f;
    const glm::vec4 foreground(0.94f, 0.97f, 1, 1), muted(0.8f, 0.86f, 0.92f, 1);
    const auto text = [&](const std::string& value, float x, float y, float size,
                          const glm::vec4& color){
        m_text.draw(value, x + 1, y + 1, size,{0, 0, 0, 0.96f});
        m_text.draw(value, x, y, size, color);
    };
    m_text.rectangle(16, top, panelWidth, panelHeight,{0.025f, 0.045f, 0.065f, 0.28f});
    text(app.m_deferredEnabled ? "PERFORMANCE / DEFERRED" : "PERFORMANCE / FORWARD", 32, top + 10,
         16, foreground);
    const auto reset = performanceResetRectangle();
    m_text.rectangle(reset.x, reset.y, reset.z, reset.w,{0.08f, 0.2f, 0.24f, 0.8f});
    text("Reset averages", reset.x + 17, reset.y + 5, 12, foreground);
    const auto fps = summary.frameMs > 0 ? decimal(1000 / summary.frameMs, 1) : "...";
    text("AVG  " + fps + " FPS   |   frame " + decimal(summary.frameMs) + " ms   |   P95 " +
             decimal(summary.p95Ms) + " ms",
         32, top + 44, 13, foreground);

    const double cpuTotal = summary.cpu[static_cast<std::size_t>(FrameProfiler::Stage::Frame)];
    const double gpuTotal = summary.gpu[static_cast<std::size_t>(FrameProfiler::Stage::Render)];
    const auto bar = [&](float y, bool gpu){
        const bool available = gpu ? summary.gpuSamples > 0 : summary.samples > 0;
        const auto time = available ? decimal(gpu ? gpuTotal : cpuTotal) + " ms"
                          : gpu && !profiler.getGpuSupported() ? "N/A"
                                                               : "...";
        text(std::string(gpu ? "GPU  " : "CPU  ") + time, 32, y - 2, 11, foreground);
        constexpr float left = 128, width = panelWidth - 144;
        m_text.rectangle(left, y, width, 13,{0.05f, 0.08f, 0.12f, 0.35f});
        float x = left;
        if (available){
            for (std::size_t i = 0; i < slices.size(); ++i){
                const auto& slice = slices[i];
                const float fraction =
                    static_cast<float>(gpu ? slice.gpuPercent : slice.cpuPercent);
                const float segment = std::min(width * fraction / 100, left + width - x);
                if (segment > 0){
                    m_text.rectangle(x, y, segment, 13, colors[i]);
                    x += segment;
                }
            }
        }
    };
    bar(top + 76, false);
    bar(top + 100, true);

    text("WHERE THE TIME GOES", 32, top + 126, 10, muted);
    text("CPU ms / %", 350, top + 126, 10, muted);
    text("GPU ms / %", 468, top + 126, 10, muted);
    for (std::size_t i = 0; i < slices.size(); ++i){
        const auto& slice = slices[i];
        const float y = top + 148 + static_cast<float>(i) * 18;
        m_text.rectangle(32, y + 3, 8, 8, colors[i]);
        text(slice.name, 48, y, 12, foreground);
        text(summary.samples ? decimal(slice.cpuMs) + " / " + decimal(slice.cpuPercent, 1) + "%"
                             : "...",
             350, y, 12, colors[i]);
        const bool cpuOnly = i == 1 || i == 6;
        const auto gpu = cpuOnly                       ? "-"
                         : !profiler.getGpuSupported() ? "N/A"
                         : !summary.gpuSamples
                             ? "..."
                             : decimal(slice.gpuMs) + " / " + decimal(slice.gpuPercent, 1) + "%";
        text(gpu, 468, y, 12, colors[i]);
    }

    const auto value = [&](FrameProfiler::Counter counter){
        return summary.counters[static_cast<std::size_t>(counter)];
    };
    using Counter = FrameProfiler::Counter;
    text("Avg draws: " + compact(value(Counter::ColorDraws)) + " color + " +
             compact(value(Counter::ShadowDraws)) +
             " shadow   |   Triangles: " + compact(value(Counter::TotalTriangles)),
         32, top + 320, 11, muted);
    text("Culling: " + compact(value(Counter::FrustumRejected) + value(Counter::PortalRejected)) +
             " subtrees cut   |   Rooms: " + decimal(value(Counter::VisibleRooms), 1) + "/" +
             std::to_string(app.m_scene->getPortalGraph().getCells().size()) + "   |   Shadows " +
             (app.m_shadowsEnabled ? "ON" : "OFF"),
         32, top + 340, 11, muted);
    text("Average window: " + std::to_string(summary.samples) + " frames / " +
             decimal(summary.frameMs * summary.samples / 1000, 1) + " s. Reset after changes.",
         32, top + 372, 11, foreground);
    text("CPU: whole frame incl. swap. GPU: elapsed render time incl. gaps. F1 / Esc: close.", 32,
         top + 393, 9, muted);
}
} // namespace vr
