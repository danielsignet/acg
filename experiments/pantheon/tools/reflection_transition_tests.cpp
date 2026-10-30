#include "GyroscopeAnimation.h"
#include "RoomIsolation.h"
#include <vr/Application.h>
#include <vr/FramebufferImage.h>
#include <vr/GLState.h>
#include <fstream>
#include <iostream>

namespace vr{
// Exercises the real application order, including mirrors, room changes and local probes.
class ReflectionTransitionValidator{
public:
    static bool run(Application& app, const std::filesystem::path& output, bool mirror){
        app.resize(960, 600);
        app.m_showMenus = false;
        app.m_animationEnabled = false;
        app.execute(Controls::Action::CookTorranceShader);
        app.execute(Controls::Action::Antialiasing);
        app.execute(Controls::Action::Reflections);
        if (!mirror){
            const auto surface = app.m_scene->findTransform("materialMirror");
            surface->setName("disabled-mirror");
            surface->setVisible(false);
        }
        const std::string prefix = mirror ? "mirror" : "no-mirror";
        bool stable = true;
        std::ofstream log(output / (prefix + ".csv"));
        log << "step,frame,ready,total,captured,filtered,mirror,changed_pixels\n";
        const auto render = [&]{
            app.update(0.5, 0);
            struct Saved{std::shared_ptr<Node> node;std::shared_ptr<State> state;bool visible;};
            std::vector<Saved> primary;
            const auto controller = app.m_scene->findTransform("Room visibility controller 001");
            const auto owner = std::dynamic_pointer_cast<Group>(controller->getParents().front().lock());
            for (const auto& child : owner->getChildren())
                if (!child->getRoomId().empty())
                    primary.push_back({child,child->getState(),child->getVisible()});
            app.render();
            for (const auto& saved : primary)
                if (saved.node->getState() != saved.state || saved.node->getVisible() != saved.visible)
                    throw std::runtime_error("An auxiliary view changed the primary scene's room state");
            return FramebufferImage::capture(0, 0, 960, 600);
        };
        const auto stop = [&](const std::string& name, const glm::vec3& eye,
                              const glm::vec3& target, unsigned frames){
            app.lookAt(eye, target);
            auto previous = render();
            const auto first = previous;
            if (name != "hall") app.m_reflectionProbes->invalidate();
            previous.save(output / (prefix + "-" + name + "-0.png"));
            std::size_t maxChange = 0;
            for (unsigned frame = 0; frame < frames; ++frame){
                auto pixels = render();
                const auto changed = pixels.countChangedPixels(previous);
                const auto& probes = *app.m_reflectionProbes;
                log << name << ',' << frame << ',' << probes.getReadyCount() << ','
                    << probes.getProbeCount() << ',' << probes.getFaces() << ','
                    << probes.getFilterDraws() << ',' << app.m_reflection->getRendered()
                    << ',' << changed << '\n';
                if (frame > 180) maxChange = std::max(maxChange, changed);
                if (frame == 12 || frame == frames - 1)
                    pixels.save(output / (prefix + "-" + name + "-" + std::to_string(frame) + ".png"));
                previous = std::move(pixels);
            }
            std::cout << prefix << ' ' << name << ": ready="
                      << app.m_reflectionProbes->getReadyCount() << '/'
                      << app.m_reflectionProbes->getProbeCount()
                      << ", late fixed-pose change=" << maxChange
                      << ", refresh changed=" << previous.countChangedPixels(first) << '\n';
            stable = stable && maxChange == 0;
            if (name != "hall") stable = stable && previous.countChangedPixels(first) == 0;
        };
        stop("hall", {0,4,-32}, {0,3,-58}, 210);
        const auto* atlas = app.m_reflectionProbes.get();
        const auto ready = atlas->getReadyCount();
        app.lookAt({0,4,-34.8f}, {0,3,-58});
        auto entered = render();
        entered.save(output / (prefix + "-threshold.png"));
        bool retained = app.m_reflectionProbes.get() == atlas &&
                        app.m_reflectionProbes->getReadyCount() >= ready;
        for (unsigned crossing = 0; crossing < 8; ++crossing){
            app.lookAt({0,4,crossing % 2 ? -34.70f : -34.60f}, {0,3,-58});
            render();
            retained = retained && app.m_reflectionProbes.get() == atlas &&
                       app.m_reflectionProbes->getReadyCount() >= ready;
        }
        std::cout << prefix << " threshold retains published maps: " << retained << '\n';
        stop("inside", {0,4,-38}, {0,3,-58}, 210);
        stop("looking-out", {0,4,-50}, {0,4,0}, 240);
        return retained && stable && app.m_reflection->getRendered() == mirror;
    }
};
}

int main(int argc, char** argv){
    GLFWwindow* window = nullptr;
    try{
        if (argc < 2) throw std::runtime_error("Expected repository root");
        const std::filesystem::path root = argv[1];
        const auto output = argc > 2 ? std::filesystem::path(argv[2]) : root / "experiments/pantheon/output/reflection-transitions";
        std::filesystem::create_directories(output);
        pantheon::registerGyroscopes();
        pantheon::registerRoomIsolation();
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(960,600,"Reflection transition tests",nullptr,nullptr);
        if (!window) throw std::runtime_error("Window creation failed");
        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            throw std::runtime_error("OpenGL loading failed");
        glfwSwapInterval(0);
        bool passed = true;
        for (const bool mirror : {true, false}){
            vr::Application app(window, root, "experiments/pantheon/scene.xml");
            vr::GLState::Scope tracked;
            passed = vr::ReflectionTransitionValidator::run(app,output,mirror) && passed;
        }
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        if (!passed) throw std::runtime_error("Crossing the room threshold discards published reflection maps");
        return 0;
    } catch(const std::exception& error){
        std::cerr << error.what() << '\n';
        if(window) glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
