#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vr/FrameProfiler.h>

namespace{
// Reports a failed measurement contract to CTest.
void check(bool value, const char* message){
    if (!value){
        throw std::runtime_error(message);
    }
}

// Checks that charts compare disjoint work against the correct CPU and GPU totals.
void breakdownTests(){
    using Profiler = vr::FrameProfiler;
    using Stage = Profiler::Stage;
    Profiler::Summary summary;
    const auto time = [&](Stage stage, double cpu, double gpu = 0){
        const auto index = static_cast<std::size_t>(stage);
        summary.cpu[index] = cpu;
        summary.gpu[index] = gpu;
    };
    time(Stage::Frame, 120);
    time(Stage::Render, 75, 40);
    time(Stage::Update, 5);
    time(Stage::Shadows, 40, 20);
    time(Stage::Portals, 1);
    time(Stage::Lights, 2);
    time(Stage::Culling, 7);
    time(Stage::Geometry, 10, 5);
    time(Stage::Lighting, 4, 4);
    time(Stage::Transparency, 1, 1);
    time(Stage::Bloom, 2, 2);
    time(Stage::DepthOfField, 1, 1);
    time(Stage::Composite, 1, 1);
    time(Stage::Present, 1, 1);
    time(Stage::Debug, 1, 0.5);
    time(Stage::Overlay, 2, 0.5);
    time(Stage::Swap, 20);
    time(Stage::Events, 11);
    time(Stage::Picking, 2);
    time(Stage::Subdivision, 6);
    const auto slices = Profiler::breakdown(summary);
    double cpuPercent = 0, gpuPercent = 0;
    for (const auto& slice : slices){
        cpuPercent += slice.cpuPercent;
        gpuPercent += slice.gpuPercent;
        check(slice.cpuPercent >= 0 && slice.gpuPercent >= 0, "Chart shares are nonnegative");
    }
    check(std::abs(cpuPercent - 100) < 1e-8 && std::abs(gpuPercent - 100) < 1e-8,
          "Each chart accounts for 100 percent without adding nested totals twice");
    check(std::abs(slices[0].cpuPercent - 100.0 / 3) < 1e-8 && slices[0].gpuPercent == 50,
          "CPU uses whole-frame time while GPU uses elapsed render time");
    check(slices[1].cpuMs == 10 && slices[1].gpuMs == 0 && slices[3].cpuMs == 5 &&
              slices[4].gpuMs == 5,
          "Related disjoint stages form useful pipeline groups");
    check(slices[6].cpuMs == 20 && slices[6].gpuPercent == 0 && slices.back().cpuMs == 9 &&
              slices.back().gpuMs == 4,
          "Swap remains CPU-only and unmeasured work stays in Other / gaps");
    check(slices[8].cpuMs == 2 && slices[9].cpuMs == 10 && slices[10].cpuMs == 6 &&
              !slices[8].gpu && !slices[9].gpu && !slices[10].gpu,
          "Raycasting is separate and event commands do not count subdivision twice");
    time(Stage::Occlusion, 2, 1);
    const auto withAO = Profiler::breakdown(summary);
    check(withAO[7].cpuMs == 2 && withAO[7].gpuMs == 1 && withAO.back().cpuMs == 7 &&
              withAO.back().gpuMs == 3,
          "Ambient occlusion has its own share without being counted again in post-processing");
    time(Stage::Forward, 3, 2);
    const auto forward = Profiler::breakdown(summary);
    check(forward[2].cpuMs == 13 && forward[2].gpuMs == 7 && forward[3].cpuMs == 5,
          "Forward surface submission belongs to geometry rather than deferred lighting");
    for (const auto& slice : Profiler::breakdown({})){
        check(slice.cpuPercent == 0 && slice.gpuPercent == 0,
              "Empty histories produce finite, empty charts");
    }
}

// Exercises nested timestamps, real frame intervals, bounded history and toggles.
void run(){
    breakdownTests();
    using Stage = vr::FrameProfiler::Stage;
    vr::FrameProfiler profiler;
    profiler.beginFrame(0.05);
    {
        vr::FrameProfiler::Scope disabled(&profiler, Stage::Render);
    }
    profiler.endFrame();
    check(profiler.getHistory().empty(), "Closed profiling records no frames");
    profiler.setEnabled(true);
    for (unsigned i = 0; i < 8; ++i){
        profiler.beginFrame(i % 2 ? 0.03 : 0.01);
        {
            vr::FrameProfiler::Scope events(&profiler, Stage::Events);
            vr::FrameProfiler::Scope subdivision(&profiler, Stage::Subdivision);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        {
            vr::FrameProfiler::Scope frame(&profiler, Stage::Frame);
            vr::FrameProfiler::Scope render(&profiler, Stage::Render);
            vr::FrameProfiler::Scope geometry(&profiler, Stage::Geometry);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        vr::FrameProfiler::Counters counters{};
        for (std::size_t counter = 0; counter < counters.size(); ++counter){
            counters[counter] = (counter + 1) * 100 + i * 0.5;
        }
        profiler.recordCounters(counters);
        profiler.endFrame();
        if (i == 0){
            check(profiler.summarize().counterSamples == 1 &&
                      profiler.summarize().counters[0] == 100,
                  "Scene counters are available without waiting for any GPU query");
        }
        // Only the test waits: production must read availability on later frames.
        glFinish();
    }
    const auto summary = profiler.summarize();
    check(summary.counterSamples == 8, "Every recorded scene frame contributes to the average");
    for (std::size_t counter = 0; counter < summary.counters.size(); ++counter){
        check(std::abs(summary.counters[counter] - ((counter + 1) * 100 + 1.75)) < 1e-8 &&
                  profiler.getCounters()[counter] == (counter + 1) * 100 + 3.5,
              "Each counter retains its live value and averages its own frame history");
    }
    check(summary.samples == 8 && std::abs(summary.frameMs - 20) < 1e-8 &&
              std::abs(summary.p95Ms - 30) < 1e-8 && summary.peakMs == 30,
          "Averages, P95 and worst frame use unclamped real frame intervals");
    check(summary.cpu[static_cast<std::size_t>(Stage::Events)] >=
                  summary.cpu[static_cast<std::size_t>(Stage::Subdivision)] &&
              summary.cpu[static_cast<std::size_t>(Stage::Subdivision)] > 0 &&
              summary.gpu[static_cast<std::size_t>(Stage::Subdivision)] == 0,
          "Event callbacks and CPU subdivision are recorded without GPU timestamp scopes");
    const auto render = static_cast<std::size_t>(Stage::Render);
    const auto geometry = static_cast<std::size_t>(Stage::Geometry);
    check(summary.cpu[render] > 0 && summary.cpu[render] >= summary.cpu[geometry],
          "Nested CPU totals include child scopes");
    check(summary.latestCpu == profiler.getHistory().back().cpu,
          "Current CPU timings come from the latest complete frame");
    if (profiler.getGpuSupported()){
        check(summary.gpuSamples == 7 && summary.gpuLag == 1,
              "GPU measurements arrive on subsequent frames");
        check(summary.gpu[render] >= summary.gpu[geometry] && summary.gpu[geometry] >= 0,
              "Nested GPU timestamps measure ordered execution");
        check(summary.latestGpu == profiler.getHistory()[6].gpu,
              "Current GPU timings come from the latest available query frame");
    }
    profiler.beginFrame(0.08);
    profiler.endFrame();
    check(profiler.summarize().counterSamples == 8 &&
              profiler.summarize().counters == summary.counters,
          "A frame without a counter snapshot does not add artificial zero measurements");
    for (std::size_t i = 0; i < vr::FrameProfiler::HistorySize + 10; ++i){
        profiler.beginFrame(0.08);
        vr::FrameProfiler::Counters counters{};
        for (std::size_t counter = 1; counter < counters.size(); ++counter){
            counters[counter] = 1000 + counter * 0.25;
        }
        profiler.recordCounters(counters);
        profiler.endFrame();
    }
    check(profiler.getHistory().size() == vr::FrameProfiler::HistorySize &&
              std::abs(profiler.summarize().frameMs - 80) < 1e-8,
          "History is bounded and preserves frames slower than the camera delta clamp");
    const auto rolling = profiler.summarize();
    check(rolling.counterSamples == vr::FrameProfiler::HistorySize && rolling.counters[0] == 0,
          "Zero counts remain valid measurements and old counter snapshots expire");
    for (std::size_t counter = 1; counter < rolling.counters.size(); ++counter){
        check(rolling.counters[counter] == 1000 + counter * 0.25,
              "Every counter uses the same bounded averaging window");
    }
    profiler.beginFrame(0.02);
    {
        vr::FrameProfiler::Scope renderScope(&profiler, Stage::Render);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    profiler.endFrame();
    glFinish();
    const auto previousSequence = profiler.getHistory().back().sequence;
    profiler.resetAverages();
    check(profiler.getEnabled() && profiler.getHistory().empty() &&
              profiler.summarize().counterSamples == 0,
          "Reset clears averages while leaving profiling enabled");
    profiler.beginFrame(0.25);
    vr::FrameProfiler::Counters fresh{};
    fresh.fill(7);
    profiler.recordCounters(fresh);
    profiler.endFrame(0.012);
    const auto reset = profiler.summarize();
    check(reset.samples == 1 && reset.gpuSamples == 0 && reset.frameMs == 12 &&
              reset.peakMs == 12 && reset.counters == fresh &&
              profiler.getHistory().back().sequence > previousSequence,
          "Reset discards old counters, timings and delayed GPU results without reusing frame IDs");
    check(reset.cpu[static_cast<std::size_t>(Stage::Frame)] == 12,
          "Frame budgets use current wall time rather than the previous configuration's interval");
    profiler.setEnabled(false);
    profiler.setEnabled(true);
    check(profiler.getHistory().empty(), "Reopening starts a fresh measurement window");
    check(profiler.getCounters() == vr::FrameProfiler::Counters{} &&
              profiler.summarize().counterSamples == 0,
          "Reopening clears live counters and their averages together");
    check(glGetError() == GL_NO_ERROR,
          "Query allocation, collection and reuse keep valid GL state");
    std::cout << "Performance measurements passed\n";
}
} // namespace

// Owns a small hidden context until every profiler query has been released.
int main(){
    if (!glfwInit()){
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(64, 64, "Performance tests", nullptr, nullptr);
    if (!window){
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    int result = 0;
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){
        result = 1;
    } else{
        try{
            run();
        } catch (const std::exception& error){
            std::cerr << "FAILED: " << error.what() << '\n';
            result = 1;
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
