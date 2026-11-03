#include <vr/Application.h>
#include <vr/BounceAnimation.h>
#include <vr/FramebufferImage.h>
#include <vr/GeometryFactory.h>
#include <vr/ShadowRenderer.h>
#include <vr/UpdateVisitor.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace{
unsigned checks=0;
void check(bool value,const char* message){ ++checks; if(!value) throw std::runtime_error(message); }
bool near(const glm::vec3& a,const glm::vec3& b){ return glm::distance(a,b)<2e-5f; }

void shader(const std::filesystem::path& root){
    auto mesh=vr::GeometryFactory::createBox();
    const auto revision=mesh->getDataRevision();
    for(bool shadow:{false,true}){
        vr::Program program(root/(shadow?"shaders/shadow-depth.vs":"shaders/phong-shading.vs"),
            root/(shadow?"shaders/shadow-depth.fs":"shaders/phong-shading.fs"),{},
            shadow?std::vector<std::string>{"shadowWorld"}:std::vector<std::string>{"worldPosition","worldNormal","worldTangent"});
        for(float time:{0.0f,.2f,.65449847f}){
            const auto deformation=vr::bounceMatrix(1,time);
            std::vector<glm::mat4> models{glm::scale(glm::rotate(glm::mat4(1),.3f,{0,1,0}),{1.3f,.7f,1.1f}),
                glm::translate(glm::mat4(1),{3,1,0})*glm::scale(glm::mat4(1),{-1,.9f,1.2f})};
            for(bool instanced:{false,true}){
                const size_t count=mesh->getIndices().size()*(instanced?models.size():1);
                const size_t stride=shadow?3:10;
                GLuint feedback=0; glGenBuffers(1,&feedback); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);
                glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,static_cast<GLsizeiptr>(count*stride*sizeof(float)),nullptr,GL_STREAM_READ);
                glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
                program.use(); program.set("uBounceRadius",1.0f); program.set("uBounceTime",time);
                program.set("uModel",models[0]); program.set("uNormal",glm::transpose(glm::inverse(glm::mat3(models[0]))));
                program.set("uView",glm::mat4(1)); program.set("uProjection",glm::mat4(1)); program.set("uLightVP",glm::mat4(1));
                program.set("uNormalMapping",true); program.set("uHasNormalMap",true);
                program.set("uTangentSign",1.0f); program.set("uInstanced",instanced);
                glEnable(GL_RASTERIZER_DISCARD); glBeginTransformFeedback(GL_TRIANGLES);
                if(instanced) mesh->drawInstanced(models,true); else mesh->draw();
                glEndTransformFeedback(); glDisable(GL_RASTERIZER_DISCARD);
                std::vector<float> values(count*stride);
                glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);
                glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,static_cast<GLsizeiptr>(values.size()*sizeof(float)),values.data());
                for(size_t i=0;i<count;++i){
                    const auto& v=mesh->getVertices()[mesh->getIndices()[i%mesh->getIndices().size()]];
                    const auto model=models[i/mesh->getIndices().size()];
                    const auto posed=model*deformation;
                    check(near({values[i*stride],values[i*stride+1],values[i*stride+2]},glm::vec3(posed*glm::vec4(v.position,1))),
                        "GPU position matches analytic bounce in color/shadow and instanced paths");
                    if(!shadow){
                        const auto normal=glm::normalize(glm::transpose(glm::inverse(glm::mat3(posed)))*v.normal);
                        check(near({values[i*stride+3],values[i*stride+4],values[i*stride+5]},normal),
                            "Squash normals use the inverse transpose, including mirrored/nonuniform instances");
                        const glm::vec3 tangent(values[i*stride+6],values[i*stride+7],values[i*stride+8]);
                        check(std::abs(glm::dot(normal,tangent))<2e-5f,"Deformed tangent remains orthogonal to the normal");
                    }
                }
                glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0); glDeleteBuffers(1,&feedback);
            }
        }
    }
    check(mesh->getDataRevision()==revision,"Vertex-shader deformation never rewrites source mesh data");
}

void animationAndPicking(const std::filesystem::path& root){
    vr::Scene scene;
    auto mesh=vr::GeometryFactory::createSphere(24,16);
    auto ball=vr::ModelInstance::create("Ball",mesh);
    auto bounce=std::make_shared<vr::BounceAnimation>(); ball->setUpdateCallback(bounce);
    scene.getRoot()->addChild(ball); vr::UpdateVisitor update;
    update.update(*scene.getRoot(),{0,0,true});
    const auto revision=mesh->getDataRevision();
    const auto envelope=mesh->getBound();
    for(int i=0;i<=100;++i){
        const auto pose=vr::bounceMatrix(1,i*.0261799388f);
        check(std::abs(glm::determinant(glm::mat3(pose))-1)<1e-5f,"Squash preserves volume");
        for(const auto& vertex:mesh->getVertices()){
            const auto p=glm::vec3(pose*glm::vec4(vertex.position,1));
            check(glm::all(glm::greaterThanEqual(p,envelope.getMinimum())) && glm::all(glm::lessThanEqual(p,envelope.getMaximum())),
                "Conservative motion bounds contain every vertex throughout the cycle");
        }
    }
    check(std::abs((vr::bounceMatrix(1,0)*glm::vec4(0,-1,0,1)).y+1)<1e-6f,"Bottom pole meets the floor during squash");
    vr::CullVisitor collector; vr::ModelPicker picker;
    const auto hit=[&](glm::vec3 eye){
        collector.collect(*scene.getRoot(),glm::mat4(1),eye,false);
        return picker.pick(collector.getRenderList(),eye,{0,0,-1});
    };
    check(bool(hit({0,0,6})),"Resting ball is selectable");
    update.update(*scene.getRoot(),{.65449847,.65449847,true});
    check(!hit({0,0,6}),"Moving ball leaves its old picking position");
    const float center=vr::bounceMatrix(1,static_cast<float>(bounce->getTime()))[3].y;
    check(bool(hit({0,center,6})),"Picking follows the GPU-deformed ball");
    const auto time=bounce->getTime(); bounce->setEnabled(false);
    update.update(*scene.getRoot(),{10,4,true});
    check(bounce->getTime()==time && !update.hasDynamicUpdates(),"Target pause holds the exact pose and stops dynamic reflection work");
    ball->setSubdivided(true);
    check(bounce->getTime()==time && ball->getSubdivided(),"Subdivision preserves the paused bounce time");
    ball->setSubdivided(false); bounce->setEnabled(true);
    update.update(*scene.getRoot(),{11,.05,false});
    check(bounce->getTime()==time,"Global scene freeze also freezes bounce");
    update.update(*scene.getRoot(),{12,.05,true});
    check(std::abs(bounce->getTime()-time-.05)<1e-6 && mesh->getDataRevision()==revision,"Resume continues from the held pose without CPU vertex updates");

    auto sun=std::make_shared<vr::Light>(); sun->setPosition({-1,2,1,0}); scene.getRoot()->addChild(sun);
    auto point=std::make_shared<vr::Light>(); point->setPosition({3,3,3,1}); scene.getRoot()->addChild(point);
    auto floor=std::make_shared<vr::Transform>();
    floor->setMatrix(glm::translate(glm::mat4(1),{0,-1.1f,0})*glm::scale(glm::mat4(1),{5,.1f,5}));
    floor->addChild(vr::GeometryFactory::createBox()); scene.getRoot()->addChild(floor);
    vr::ShadowRenderer cached(root,64,64),reference(root,64,64); cached.attach(scene); reference.attach(scene); reference.setCaching(false);
    std::vector<float> previous;
    for(int step=0;step<4;++step){
        update.update(*scene.getRoot(),{step*.2,.2,true});
        cached.render(scene,{0,2,6},true,true);
        auto depth=cached.getSunTarget()->readDepth(0);
        std::array<std::vector<float>,6> cube;
        for(unsigned face=0;face<6;++face) cube[face]=cached.getPointTarget()->readDepth(face);
        reference.render(scene,{0,2,6},true,true);
        const auto compare=[&](const auto& a,const auto& b){
            check(a.size()==b.size(),"Matching shadow target size");
            for(size_t i=0;i<a.size();++i) check(std::abs(a[i]-b[i])<2e-6f,"Cached shadows match full rerender during bounce");
        };
        compare(depth,reference.getSunTarget()->readDepth(0));
        for(unsigned face=0;face<6;++face) compare(cube[face],reference.getPointTarget()->readDepth(face));
        if(step) check(depth!=previous,"Animated bounce invalidates stale directional shadow depth");
        previous=std::move(depth);
    }
    bounce->setEnabled(false); update.update(*scene.getRoot(),{2,.5,true});
    cached.render(scene,{0,2,6},true,true); cached.render(scene,{0,2,6},true,true);
    check(cached.getStats().draws==0,"Paused bounce reuses unchanged cached shadow faces");
}

void interaction(const std::filesystem::path& root,GLFWwindow* window){
    const auto file=std::filesystem::temp_directory_path()/("acg-bounce-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".xml");
    struct Remove{std::filesystem::path p;~Remove(){std::error_code error;std::filesystem::remove(p,error);}} remove{file};
    std::ofstream(file)<<R"(<scene><camera eye="0 1.1 6" target="0 1.1 0"/><group><state lighting="false"/>
<transform translate="0 1.1 0"><instance ref="material-sphere" name="Ball" selectable="true" bounceRadius="1" bouncePaused="true"/></transform>
</group></scene>)";
    vr::Application app(window,root,file); app.resize(512,400); app.handleKey(GLFW_KEY_TAB);
    app.update(0,0); app.render();
    // Same target action rectangle as subdivision, next row down.
    app.handleClick(290,296); app.update(.03,.03); app.render();
    const auto moving=vr::FramebufferImage::capture(0,0,512,400);
    app.handleClick(290,296); app.update(1,.3); app.render();
    const auto paused=vr::FramebufferImage::capture(0,0,512,400);
    app.update(2,.5); app.render();
    check(paused.hash()==vr::FramebufferImage::capture(0,0,512,400).hash(),"Aim-and-click pause freezes the displayed pose exactly");
    check(moving.hash()!=paused.hash(),"Target action changes its displayed Pause/Resume state");
    app.handleKey(GLFW_KEY_P); app.update(3,.03); app.render();
    check(paused.countChangedPixels(vr::FramebufferImage::capture(0,0,512,400),0)>100,"P resumes only the targeted bouncing model");
}

void spinInteraction(const std::filesystem::path& root,GLFWwindow* window){
    const auto file=std::filesystem::temp_directory_path()/("acg-spin-pause-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".xml");
    struct Remove{std::filesystem::path p;~Remove(){std::error_code e;std::filesystem::remove(p,e);}} cleanup{file};
    std::ofstream(file)<<R"(<scene><camera eye="1.3 3 6" target="1.3 0 0"/>
<group><state lighting="false" diffuse=".8 .25 .1"/>
<transform name="animated" animation="spin" axis="0 0 1" speed="90"><group>
<instance ref="torus" name="Doughnut" selectable="true"/></group></transform></group></scene>)";
    vr::Application app(window,root,file);app.resize(512,400);app.handleKey(GLFW_KEY_TAB);
    app.update(0,0);app.render();
    app.handleKey(GLFW_KEY_P);app.update(1,.3);app.render();
    const auto paused=vr::FramebufferImage::capture(0,0,512,400);
    app.update(2,.5);app.render();
    check(paused.hash()==vr::FramebufferImage::capture(0,0,512,400).hash(),
          "P pauses the targeted doughnut's ancestor spin without changing its pose");
    app.handleKey(GLFW_KEY_P);app.update(3,.03);app.render();
    const auto moving=vr::FramebufferImage::capture(0,0,512,400);
    check(paused.countChangedPixels(moving)>100,"P resumes the targeted doughnut");
    app.handleClick(290,296);app.update(4,.5);app.render();
    const auto clicked=vr::FramebufferImage::capture(0,0,512,400);
    app.update(5,.5);app.render();
    check(clicked.hash()==vr::FramebufferImage::capture(0,0,512,400).hash(),
          "The shared Pause button also freezes the doughnut");
    app.handleKey(GLFW_KEY_Z);app.handleKey(GLFW_KEY_P);app.handleKey(GLFW_KEY_ESCAPE);
    app.handleKey(GLFW_KEY_TAB); // Opening the shader menu shows the menus again.
    app.update(6,.03);app.render();
    check(clicked.countChangedPixels(vr::FramebufferImage::capture(0,0,512,400))>100,
          "P still resumes animation while the shader menu is open");
}
}

int main(int argc,char** argv){
    if(argc!=2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(512,400,"Vertex bounce tests",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;} glfwMakeContextCurrent(window);
    int result=0;
    try{
        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) throw std::runtime_error("OpenGL failed");
        shader(argv[1]); animationAndPicking(argv[1]); interaction(argv[1],window);spinInteraction(argv[1],window);
        check(glGetError()==GL_NO_ERROR,"Bounce paths leave valid GL state");
        std::cout<<"Vertex bounce: "<<checks<<" checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
