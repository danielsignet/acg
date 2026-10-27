#include <vr/ResourceLocator.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vr/Viewer.h>

namespace vr{
// Releases scene resources before destroying their OpenGL context.
Viewer::~Viewer(){
    m_application.reset();
    if (m_window){
        glfwDestroyWindow(m_window);
    }
    if (m_initialized){
        glfwTerminate();
    }
}

// Reads command-line options and resolves the application resource root.
bool Viewer::parseOptions(int argc, char** argv){
    for (int i = 1; i < argc; ++i){
        const std::string option = argv[i];
        const auto next = [&]{
            if (i + 1 >= argc){
                throw std::invalid_argument("Missing value for " + option);
            }
            return std::string(argv[++i]);
        };
        if (option == "--aa"){
            const auto mode = next();
            if (mode != "off" && mode != "fxaa" && mode != "msaa"){
                throw std::invalid_argument("Unknown antialiasing mode: " + mode);
            }
            m_antialiasingMode = mode == "fxaa"   ? AntialiasingMode::Fxaa
                                 : mode == "msaa" ? AntialiasingMode::Msaa
                                                  : AntialiasingMode::Off;
        } else if (option == "--forward"){
            m_forward = true;
        } else if (option == "--no-ao"){
            m_noAO = true;
        } else if (option == "--performance"){
            m_performance = true;
        } else if (option == "--hidden"){
            m_hidden = true;
        } else if (option == "--validate"){
            m_validate = true;
        } else if (option == "--lod-demo"){
            m_lodDemoCallback = true;
        } else if (option == "--inside"){
            m_inside = true;
        } else if (option == "--bloom"){
            m_bloom = true;
        } else if (option == "--dof"){
            m_depthOfField = true;
        } else if (option == "--no-buffer-previews"){
            m_noBufferPreviews = true;
        } else if (option == "--buffer"){
            const auto view = next();
            const std::array<std::string, 10> names{{"lit", "positions", "normals", "albedo",
                                                     "specular", "depth", "ambient", "emissive",
                                                     "plane", "ao"}};
            const auto found = std::find(names.begin(), names.end(), view);
            if (found == names.end()){
                throw std::invalid_argument("Unknown buffer view: " + view);
            }
            m_bufferView = static_cast<BufferView>(std::distance(names.begin(), found));
        } else if (option == "--no-culling"){
            m_noCulling = true;
        } else if (option == "--no-shadows"){
            m_noShadows = true;
        } else if (option == "--portal-demo"){
            m_portalDemo = true;
        } else if (option == "--no-portals"){
            m_noPortals = true;
        } else if (option == "--solid-walls"){
            m_solidWalls = true;
        } else if (option == "--frames"){
            m_frameLimit = std::stoi(next());
            if (m_frameLimit < 1){
                throw std::invalid_argument("--frames must be positive");
            }
        } else if (option == "--root"){
            m_root = next();
        } else if (option == "--scene"){
            m_scene = next();
        } else if (option == "--screenshot"){
            m_screenshot = next();
        } else if (option == "--dot"){
            m_dotFile = next();
        } else if (option == "--help"){
            std::cout << "viewer [--root path] [--scene file] [--frames N] [--hidden] [--validate] "
                         "[--lod-demo] [--inside] [--no-culling] "
                         "[--portal-demo] [--no-portals] [--solid-walls] [--no-shadows] "
                         "[--buffer lit|positions|normals|albedo|specular|depth|"
                         "ambient|emissive|plane|ao] [--bloom] [--dof] "
                         "[--no-buffer-previews] [--performance] [--forward] [--no-ao] "
                         "[--aa off|fxaa|msaa] "
                         "[--screenshot file.png] [--dot file.dot]\n";
            return false;
        } else{
            throw std::invalid_argument("Unknown option: " + option);
        }
    }
    m_root = ResourceLocator::findRoot(argv[0], m_root);
    return true;
}

// Creates the OpenGL context before constructing application resources.
void Viewer::initialize(){
    glfwSetErrorCallback([](int code, const char* message){
        std::cerr << "GLFW " << code << ": " << message << '\n';
    });
    if (!glfwInit()){
        throw std::runtime_error("GLFW initialization failed");
    }
    m_initialized = true;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_VISIBLE, m_hidden ? GLFW_FALSE : GLFW_TRUE);
    m_window = glfwCreateWindow(1280, 800, Application::WindowTitle, nullptr, nullptr);
    if (!m_window){
        throw std::runtime_error("Window creation failed");
    }
    glfwSetWindowSizeLimits(m_window, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(m_window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){
        throw std::runtime_error("OpenGL loading failed");
    }
    glfwSwapInterval(m_hidden ? 0 : 1);
    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\nRenderer: " << glGetString(GL_RENDERER)
              << "\nGLSL: " << glGetString(GL_SHADING_LANGUAGE_VERSION)
              << "\nAssets: " << m_root.string() << '\n';
    m_application = std::make_unique<Application>(m_window, m_root, m_scene, m_dotFile);
    m_application->m_vsync = !m_hidden;
    glfwSetWindowUserPointer(m_window, m_application.get());
    glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int, int action, int){
        if (action == GLFW_PRESS){
            static_cast<Application*>(glfwGetWindowUserPointer(window))->handleKey(key);
        }
    });
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, int width, int height){
        static_cast<Application*>(glfwGetWindowUserPointer(window))->resize(width, height);
    });
    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int){
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS){
            return;
        }
        double x, y;
        int windowWidth, windowHeight, width, height;
        glfwGetCursorPos(window, &x, &y);
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(window, &width, &height);
        if (windowWidth > 0 && windowHeight > 0){
            static_cast<Application*>(glfwGetWindowUserPointer(window))
                ->handleClick(static_cast<float>(x * width / windowWidth),
                              static_cast<float>(y * height / windowHeight));
        }
    });
}

// Executes the configured application or validation sequence.
int Viewer::run(int argc, char** argv){
    if (!parseOptions(argc, argv)){
        return 0;
    }
    initialize();
    int width;
    int height;
    glfwGetFramebufferSize(m_window, &width, &height);
    m_application->resize(width, height);
    if (m_validate){
        m_application->validate();
    }
    while (m_application->m_antialiasingMode != m_antialiasingMode){
        const auto previousMode = m_application->m_antialiasingMode;
        m_application->execute(Controls::Action::Antialiasing);
        if (previousMode == m_application->m_antialiasingMode){
            throw std::runtime_error(m_application->m_status);
        }
    }
    if (m_lodDemoCallback){
        m_application->execute(Controls::Action::LodTour);
    }
    if (m_inside){
        m_application->execute(Controls::Action::HouseView);
    }
    if (m_noShadows){
        m_application->execute(Controls::Action::Shadows);
    }
    if (m_noCulling){
        m_application->execute(Controls::Action::Culling);
    }
    if (m_portalDemo){
        m_application->execute(Controls::Action::CullingDemo);
    }
    if (m_noPortals){
        m_application->execute(Controls::Action::PortalCulling);
    }
    if (m_solidWalls){
        m_application->execute(Controls::Action::TransparentWalls);
    }
    if (m_bloom){
        m_application->execute(Controls::Action::Bloom);
    }
    if (m_depthOfField){
        m_application->execute(Controls::Action::DepthOfField);
    }
    if (m_noBufferPreviews){
        m_application->execute(Controls::Action::BufferPreviews);
    }
    if (m_bufferView != BufferView::Lit){
        m_application->execute(
            static_cast<Controls::Action>(static_cast<unsigned>(Controls::Action::PositionBuffer) +
                                          static_cast<unsigned>(m_bufferView) - 1));
    }
    if (m_noAO){
        m_application->execute(Controls::Action::AmbientOcclusion);
    }
    if (m_forward){
        m_application->execute(Controls::Action::Renderer);
    }
    if (m_performance){
        m_application->execute(Controls::Action::Performance);
    }
    double previous = glfwGetTime();
    int frames = 0;
    while (!glfwWindowShouldClose(m_window)){
        const double frameStart = glfwGetTime();
        auto& profiler = m_application->m_profiler;
        profiler.beginFrame(frameStart - previous);
        {
            FrameProfiler::Scope events(&profiler, FrameProfiler::Stage::Events);
            glfwPollEvents();
        }
        const double now = glfwGetTime();
        const double interval = now - previous;
        const double delta = std::min(interval, 0.05);
        previous = now;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width == 0 || height == 0){
            profiler.endFrame(glfwGetTime() - frameStart);
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        {
            FrameProfiler::Scope frame(&profiler, FrameProfiler::Stage::Frame);
            {
                FrameProfiler::Scope update(&profiler, FrameProfiler::Stage::Update);
                m_application->update(now, delta);
            }
            m_application->render();
            ++frames;
            if (m_application->consumeScreenshotRequest()){
                FramebufferImage::capture(0, 0, width, height)
                    .save(ResourceLocator::temporaryOutput("capture.png"));
            }
            if (m_frameLimit && frames >= m_frameLimit && !m_screenshot.empty()){
                FramebufferImage::capture(0, 0, width, height).save(m_screenshot);
            }
            {
                FrameProfiler::Scope swap(&profiler, FrameProfiler::Stage::Swap);
                glfwSwapBuffers(m_window);
            }
        }
        profiler.endFrame(glfwGetTime() - frameStart);
        if (m_frameLimit && frames >= m_frameLimit){
            break;
        }
    }
    return 0;
}
} // namespace vr
