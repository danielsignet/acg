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
        if (option == "--hidden"){
            m_hidden = true;
        } else if (option == "--validate"){
            m_validate = true;
        } else if (option == "--lod-demo"){
            m_lodDemoCallback = true;
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
                         "[--lod-demo] [--screenshot file.png] [--dot file.dot]\n";
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
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_VISIBLE, m_hidden ? GLFW_FALSE : GLFW_TRUE);
    m_window = glfwCreateWindow(1280, 800, Application::WindowTitle, nullptr, nullptr);
    if (!m_window){
        throw std::runtime_error("Window creation failed");
    }
    glfwMakeContextCurrent(m_window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))){
        throw std::runtime_error("OpenGL loading failed");
    }
    glfwSwapInterval(m_hidden ? 0 : 1);
    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\nRenderer: " << glGetString(GL_RENDERER)
              << "\nGLSL: " << glGetString(GL_SHADING_LANGUAGE_VERSION)
              << "\nAssets: " << m_root.string() << '\n';
    m_application = std::make_unique<Application>(m_window, m_root, m_scene, m_dotFile);
    glfwSetWindowUserPointer(m_window, m_application.get());
    glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int, int action, int){
        if (action == GLFW_PRESS){
            static_cast<Application*>(glfwGetWindowUserPointer(window))->handleKey(key);
        }
    });
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, int width, int height){
        static_cast<Application*>(glfwGetWindowUserPointer(window))->resize(width, height);
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
    if (m_lodDemoCallback){
        m_application->handleKey(GLFW_KEY_V);
    }
    double previous = glfwGetTime();
    int frames = 0;
    while (!glfwWindowShouldClose(m_window)){
        glfwPollEvents();
        const double now = glfwGetTime();
        const double delta = std::min(now - previous, 0.05);
        previous = now;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width == 0 || height == 0){
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        m_application->update(now, delta);
        m_application->render();
        ++frames;
        if (m_application->consumeScreenshotRequest()){
            FramebufferImage::capture(0, 0, width, height)
                .save(ResourceLocator::temporaryOutput("capture.png"));
        }
        if (m_frameLimit && frames >= m_frameLimit && !m_screenshot.empty()){
            FramebufferImage::capture(0, 0, width, height).save(m_screenshot);
        }
        glfwSwapBuffers(m_window);
        if (m_frameLimit && frames >= m_frameLimit){
            break;
        }
    }
    return 0;
}
} // namespace vr
