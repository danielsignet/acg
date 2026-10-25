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
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<Application> m_application;
    std::filesystem::path m_root;
    std::filesystem::path m_scene = "scenes/house-patio.xml";
    std::filesystem::path m_screenshot;
    std::filesystem::path m_dotFile;
    bool m_initialized = false;
    bool m_hidden = false;
    bool m_validate = false;
    bool m_lodDemoCallback = false;
    bool m_inside = false;
    bool m_noCulling = false;
    bool m_noShadows = false;
    bool m_portalDemo = false, m_noPortals = false, m_solidWalls = false;
    int m_frameLimit = 0;
};
} // namespace vr
