#pragma once
#include "Application.h"

namespace vr{
// Owns the GLFW/OpenGL lifetime; Application is released before its context.
class Viewer final{
public:
    // Initializes an empty window and OpenGL-context owner.
    Viewer() = default;

    // Releases scene resources before destroying their OpenGL context.
    ~Viewer();

    // Prevents copying ownership or identity of this object.
    Viewer(const Viewer&) = delete;

    // Prevents copying ownership or identity of this object.
    Viewer& operator=(const Viewer&) = delete;

    // Executes the configured application or validation sequence.
    int run(int argc, char** argv);

private:
    // Reads command-line options and resolves the application resource root.
    bool parseOptions(int argc, char** argv);

    // Creates the OpenGL context before constructing application resources.
    void initialize();
    void writeBenchmark() const;
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<Application> m_application;
    std::filesystem::path m_root;
    std::filesystem::path m_scene = "scenes/house-patio.xml";
    std::filesystem::path m_screenshot;
    std::filesystem::path m_dotFile;
    std::filesystem::path m_benchmark;
    bool m_initialized = false;
    bool m_hidden = false;
    bool m_validate = false;
    bool m_performance = false;
    bool m_noMenus = false;
    bool m_forward = false, m_noAO = false;
    bool m_noBump = false;
    bool m_noVsync = false;
    bool m_cpuSkinning = false;
    bool m_lodDemoCallback = false;
    bool m_inside = false;
    bool m_noCulling = false;
    bool m_bloom = false, m_depthOfField = false, m_noBufferPreviews = false;
    BufferView m_bufferView = BufferView::Lit;
    AntialiasingMode m_antialiasingMode = AntialiasingMode::Off;
    ReflectionMode m_reflectionMode = ReflectionMode::Environment;
    ShadingMode m_startShading = ShadingMode::Phong;
    bool m_noShadows = false;
    bool m_portalDemo = false, m_noPortals = false, m_solidWalls = false;
    int m_frameLimit = 0;
};
} // namespace vr
