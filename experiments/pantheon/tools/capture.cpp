#include <vr/Application.h>
#include <vr/FramebufferImage.h>
#include <vr/GLState.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include "GyroscopeAnimation.h"
#include "RoomIsolation.h"

// Same Application and renderer as viewer.exe. Advance animation once to a fixed
// pose, then capture both culling modes without wall-clock-dependent animation.
int main(int argc, char** argv){
    if (argc < 4){
        std::cerr << "capture root scene output-prefix [dot-path] [pose-time]\n";
        return 2;
    }
    GLFWwindow* window = nullptr;
    try{
        pantheon::registerGyroscopes();
        pantheon::registerRoomIsolation();
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(1280, 800, "Pantheon fixed-pose verification", nullptr, nullptr);
        if (!window) throw std::runtime_error("Window creation failed");
        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            throw std::runtime_error("OpenGL initialization failed");
        glfwSwapInterval(0);
        {
            vr::Application app(window, argv[1], argv[2], argc > 4 ? argv[4] : "");
            vr::GLState::Scope trackedContext;
            app.resize(1280, 800);
            const std::string mode=argc>6 ? argv[6] : "deferred";
            if (mode=="forward") app.handleKey(GLFW_KEY_F2);
            if (mode=="no-shadows") app.handleKey(GLFW_KEY_J);
            if (mode=="no-normal") app.handleKey(GLFW_KEY_F6);
            if (mode=="dof") app.handleKey(GLFW_KEY_V);
            if (mode=="cubemaps") app.handleKey(GLFW_KEY_F5);
            const double poseTime=argc>5 ? std::stod(argv[5]) : .5;
            app.update(poseTime, poseTime);
            if (mode=="cubemaps"){
                for(int frame=0;frame<180;++frame){
                    app.update(poseTime,0);app.render();glfwSwapBuffers(window);
                }
            }
            const auto capture = [&](const char* suffix){
                for (int frame = 0; frame < 8; ++frame){
                    glfwPollEvents();
                    app.update(poseTime, 0.0);
                    app.render();
                    if (frame == 7){
                        vr::FramebufferImage::capture(0, 0, 1280, 800)
                            .save(std::filesystem::path(std::string(argv[3]) + suffix + ".png"));
                        if (std::string(suffix)=="-lit-on"){
                            std::vector<float> depth(1280*800);
                            glReadPixels(0,0,1280,800,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
                            std::ofstream mask(std::string(argv[3])+"-background.pgm",std::ios::binary);
                            mask << "P5\n1280 800\n255\n";
                            for (int y=799;y>=0;--y) for(int x=0;x<1280;++x){
                                const unsigned char value=depth[y*1280+x]==1.0f ? 255 : 0;
                                mask.write(reinterpret_cast<const char*>(&value),1);
                            }
                        }
                    }
                    glfwSwapBuffers(window);
                }
            };
            app.handleKey(GLFW_KEY_Z);
            app.handleKey(GLFW_KEY_7);
            app.handleKey(GLFW_KEY_O); // depth: no AO, AA remains off
            app.handleKey(GLFW_KEY_TAB); // selecting a shader opens the menus
            capture("-depth-on");
            app.handleKey(GLFW_KEY_C);
            capture("-depth-unculled");
            app.handleKey(GLFW_KEY_Z);
            app.handleKey(GLFW_KEY_P);
            app.handleKey(GLFW_KEY_TAB);
            app.handleKey(GLFW_KEY_O);
            if (mode=="no-shadows") app.handleKey(GLFW_KEY_O);
            app.handleKey(GLFW_KEY_F3); // lit: Cook-Torrance, AO and FXAA
            if (mode=="msaa") app.handleKey(GLFW_KEY_F3);
            if (mode=="cubemaps"){
                // Shader selection changed from the depth/Phong setup to Cook.
                // Finish that bake before comparing two culling states.
                for(int frame=0;frame<180;++frame){app.update(poseTime,0);app.render();glfwSwapBuffers(window);}
            }
            app.handleKey(GLFW_KEY_C);
            capture("-lit-on");
            app.handleKey(GLFW_KEY_C);
            capture("-lit-unculled");
            if(mode=="cubemaps"){
                for(int frame=0;frame<180;++frame){app.update(poseTime,0);app.render();glfwSwapBuffers(window);}
                for(int frame=0;frame<40;++frame){
                    app.update(poseTime,0);app.render();
                    if(frame%2==0) vr::FramebufferImage::capture(0,0,1280,800)
                        .save(std::string(argv[3])+"-stable-"+std::to_string(frame)+".png");
                    glfwSwapBuffers(window);
                }
            }
            std::ofstream report(std::string(argv[3])+"-portals.json");
            report << "[";
            bool first=true;
            for(const auto& item:pantheon::RoomPortalViews::lastCaptures){
                if(!first) report << ",";
                first=false;
                report << "{\"room\":\"" << item.room << "\",\"width\":" << item.width
                       << ",\"height\":" << item.height << ",\"draws\":" << item.draws << "}";
            }
            report << "]\n";
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
