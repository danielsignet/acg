#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vr/FrameProfiler.h>

namespace{
// Reports a failed measurement contract to CTest.
void check(bool value, const char* message){
    if (!value){
        throw std::runtime_error(message);
    }
}

// Exercises nested timestamps, real frame intervals, bounded history and toggles.
void run(){
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
              std::abs(summary.p95Ms - 30) < 1e-8,
          "Averages and P95 use unclamped real frame intervals");
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
