#include <vr/Billboard.h>
#include <vr/Antialiasing.h>
#include <vr/Camera.h>
#include <vr/DeferredRenderer.h>
#include <vr/FramebufferImage.h>
#include <vr/GeometryFactory.h>
#include <vr/Reader.h>
#include <vr/RenderVisitor.h>
#include <vr/ShadowCollector.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace{
unsigned checks = 0;
void check(bool ok, const char* message){ ++checks; if (!ok) throw std::runtime_error(message); }
bool contains(const vr::BoundingBox& box, const glm::vec3& p){
    return glm::all(glm::greaterThanEqual(p, box.getMinimum()-glm::vec3(1e-4f))) &&
           glm::all(glm::lessThanEqual(p, box.getMaximum()+glm::vec3(1e-4f)));
}

void orientation(){
    auto card = std::make_shared<vr::Billboard>();
    auto mesh = vr::GeometryFactory::createAlphaTestPanel(); card->addChild(mesh);
    const auto local = glm::translate(glm::mat4(1), {2, .7f, -3}) *
        glm::scale(glm::mat4(1), {.6f, 1.2f, .6f});
    card->setMatrix(local);
    const auto bound = card->getBound(); const auto builds = card->getBoundBuildCount();
    auto shear = glm::scale(glm::rotate(glm::mat4(1), .4f, {1,0,1}), {-2, .7f, 1.3f});
    shear[1][0] += .2f;
    for (const auto& parent : {glm::mat4(1), shear}){
        const auto base = parent * local;
        const glm::vec3 anchor(base[3]); const auto up = glm::normalize(glm::vec3(base[1]));
        const auto worldBound = bound.transformed(parent);
        for (unsigned step = 0; step < 72; ++step){
            const float a = step * 6.2831853f / 72;
            const auto eye = anchor + glm::vec3(8*std::sin(a), 4, 8*std::cos(a));
            const auto model = card->facingMatrix(parent, eye);
            auto desired = eye-anchor; desired -= up*glm::dot(up, desired);
            const auto normal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(model))) * glm::vec3(0,0,1));
            check(glm::dot(normal, glm::normalize(desired)) > .99999f,
                  "Billboard normal faces the observer projected around the constrained axis, including shear");
            check(glm::distance(glm::vec3(model[3]), anchor) < 1e-5f &&
                      glm::distance(glm::vec3(model[1]), glm::vec3(base[1])) < 1e-5f,
                  "Facing preserves the ground anchor, height and transformed up axis");
            if (parent==glm::mat4(1))
                check(std::abs(glm::length(glm::vec3(model[0]))-.6f)<1e-5f,
                      "Equal radial scales preserve grass width around a complete camera orbit");
            for (const auto& vertex : mesh->getVertices())
                check(contains(worldBound, glm::vec3(model*glm::vec4(vertex.position,1))),
                      "Cached parent bounds contain every facing orientation");
        }
        const auto overhead = card->facingMatrix(parent, anchor+up*5.0f);
        for (unsigned c=0;c<4;++c) for(unsigned r=0;r<4;++r)
            check(std::isfinite(overhead[c][r]), "An observer on the constrained axis has a finite fallback");
    }
    check(card->getMatrix()==local && card->getBoundBuildCount()==builds,
          "Changing observers never mutates the shared transform or dirties its bound");

    auto root = std::make_shared<vr::Group>(); auto state = std::make_shared<vr::State>();
    state->setCastShadows(true); root->setState(state); root->addChild(card);
    auto other = std::make_shared<vr::Transform>();other->setMatrix(glm::translate(glm::mat4(1),{10,0,0}));
    other->addChild(card);root->addChild(other);
    vr::CullVisitor culler;
    culler.collect(*root, glm::mat4(1), {0,2,8}, false);
    check(culler.getRenderList().size()==2 && culler.getRenderList()[0].model!=culler.getRenderList()[1].model,
          "A shared billboard has distinct per-path placement and facing");
    vr::ShadowCollector shadows;
    const auto first = shadows.collect(*root, {0,2,8});
    const auto second = shadows.collect(*root, {8,2,0});
    check(first.size()==2 && second.size()==2 && first[0].model!=second[0].model,
          "Cached shadow branches refresh their facing when the observer moves, even without LOD");
    shadows.setStateCaching(false);
    const auto uncached = shadows.collect(*root,{8,2,0});
    check(uncached[0].model==second[0].model, "Cached and uncached shadow facing agree");
    vr::Camera camera;camera.setPerspective(50,1,.1f,100);camera.setLookAt({10,2,-3},{2,2,-3});
    culler.collect(*root,camera.getProjection()*camera.getView(),camera.getPosition());
    check(!culler.getRenderList().empty(), "Hierarchical culling retains a card seen edge-on in its rest pose");
}

void rendering(const std::filesystem::path& root){
    auto program = std::make_shared<vr::Program>(root/"shaders/phong-shading.vs",root/"shaders/phong-shading.fs");
    auto graph=std::make_shared<vr::Group>();auto card=std::make_shared<vr::Billboard>();graph->addChild(card);
    auto mesh=vr::GeometryFactory::createAlphaTestPanel();card->addChild(mesh);
    auto state=std::make_shared<vr::State>();state->setLighting(false);state->setCullFace(false);
    state->setUniform("uAlphaCutoff",.5f);state->setUniform("uFog",false);graph->setState(state);
    // Left half is transparent, right half opaque: expose missing cutouts in any pass.
    std::vector<unsigned char> rgba(16*16*4,255);
    for(unsigned y=0;y<16;++y) for(unsigned x=0;x<8;++x) rgba[(y*16+x)*4+3]=0;
    auto texture=std::make_shared<vr::Texture>(16,16,rgba);state->setTexture(0,texture);
    vr::RenderVisitor renderer(program);vr::Camera camera;camera.setPerspective(45,1,.1f,20);
    vr::DeferredRenderer deferred(root);deferred.resize(256,256);
    vr::Antialiasing forwardAA(root);
    for(bool useDeferred:{false,true}) for(auto aa:{vr::AntialiasingMode::Off,vr::AntialiasingMode::Fxaa,vr::AntialiasingMode::Msaa}){
        deferred.setAntialiasing(aa);
        forwardAA.configure(256,256,aa);
        for(const auto& eye:{glm::vec3(0,1.4f,6),glm::vec3(6,1.4f,0),glm::vec3(0,1.4f,-6)}){
            camera.setLookAt(eye,{0,1.4f,0});glViewport(0,0,256,256);glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            if (useDeferred) renderer.render(*graph,camera,nullptr,nullptr,&deferred);
            else forwardAA.renderForward([&]{renderer.render(*graph,camera);},nullptr);
            std::vector<unsigned char> pixels(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            unsigned opaque=0, holes=0;
            for(unsigned y=90;y<165;++y) for(unsigned x=95;x<160;++x){
                const auto value=pixels[(y*256+x)*4];opaque+=value>100;holes+=value<10;
            }
            check(opaque>1000 && holes>1000,
                  "Forward/deferred and all AA modes retain both visible leaves and alpha holes from each viewpoint");
        }
    }
    // Run the actual shadow shaders using exactly the facing matrix collected above.
    vr::Program depth(root/"shaders/shadow-depth.vs",root/"shaders/shadow-depth.fs");
    camera.setLookAt({6,1.4f,0},{0,1.4f,0});
    glViewport(0,0,256,256);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_CULL_FACE);
    glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);depth.use();
    depth.set("uModel",card->facingMatrix(glm::mat4(1),camera.getPosition()));
    depth.set("uLightVP",camera.getProjection()*camera.getView());depth.set("uInstanced",false);
    depth.set("uHasAlbedo",true);depth.set("uPointPass",false);depth.set("uAlbedo",0);
    depth.set("uAlphaCutoff",.5f);depth.set("uLodRange",glm::vec2(0,1));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture->getId());mesh->draw();
    std::vector<float> values(256*256);glReadPixels(0,0,256,256,GL_DEPTH_COMPONENT,GL_FLOAT,values.data());
    unsigned solid=0, holes=0;
    for(unsigned y=90;y<165;++y) for(unsigned x=95;x<160;++x){solid+=values[y*256+x]<.99f;holes+=values[y*256+x]>.999f;}
    check(solid>1000 && holes>1000, "Shadow maps preserve the billboard's alpha silhouette and holes");
}

void reading(const std::filesystem::path& root){
    const auto file=std::filesystem::temp_directory_path()/("acg-billboard-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".xml");
    struct Remove{std::filesystem::path path;~Remove(){std::error_code e;std::filesystem::remove(path,e);}} cleanup{file};
    std::ofstream(file)<<R"(<scene><assets><model id="grass-card" file="experiments/pantheon/meshes/billboard-grass.obj"/></assets><billboard name="grass" translate="1 2 3" scale=".5 .4 1"><instance ref="grass-card"/></billboard></scene>)";
    vr::Reader reader(root);auto scene=reader.readScene(file);
    check(std::dynamic_pointer_cast<vr::Billboard>(scene->findTransform("grass"))!=nullptr,
          "XML creates a reusable billboard node with ordinary transform/state/instance support");
    std::ofstream(file)<<R"(<scene><billboard mode="advertisement"/></scene>)";
    bool rejected=false;try{reader.readScene(file);}catch(const std::exception&){rejected=true;}
    check(rejected,"An unsupported billboard definition is rejected explicitly");
}
}
int main(int argc,char** argv){
    if(argc!=2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(256,256,"Billboard tests",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);int result=0;
    try{
        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) throw std::runtime_error("OpenGL failed");
        orientation();rendering(argv[1]);reading(argv[1]);
        check(glGetError()==GL_NO_ERROR,"Billboard passes leave valid OpenGL state");
        std::cout<<"Billboards: "<<checks<<" checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
