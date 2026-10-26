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
        profiler.endFrame();
        // Only the test waits: production must read availability on later frames.
        glFinish();
    }
    const auto summary = profiler.summarize();
    check(summary.samples == 8 && std::abs(summary.frameMs - 20) < 1e-8 &&
              std::abs(summary.p95Ms - 30) < 1e-8,
          "Averages and P95 use unclamped real frame intervals");
    const auto render = static_cast<std::size_t>(Stage::Render);
    const auto geometry = static_cast<std::size_t>(Stage::Geometry);
    check(summary.cpu[render] > 0 && summary.cpu[render] >= summary.cpu[geometry],
          "Nested CPU totals include child scopes");
    if (profiler.getGpuSupported()){
        check(summary.gpuSamples == 7 && summary.gpuLag == 1,
              "GPU measurements arrive on subsequent frames");
        check(summary.gpu[render] >= summary.gpu[geometry] && summary.gpu[geometry] >= 0,
              "Nested GPU timestamps measure ordered execution");
    }
    for (std::size_t i = 0; i < vr::FrameProfiler::HistorySize + 10; ++i){
        profiler.beginFrame(0.08);
        profiler.endFrame();
    }
    check(profiler.getHistory().size() == vr::FrameProfiler::HistorySize &&
              std::abs(profiler.summarize().frameMs - 80) < 1e-8,
          "History is bounded and preserves frames slower than the camera delta clamp");
    profiler.setEnabled(false);
    profiler.setEnabled(true);
    check(profiler.getHistory().empty(), "Reopening starts a fresh measurement window");
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
