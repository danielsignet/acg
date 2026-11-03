#include <vr/Application.h>
#include <vr/Fur.h>
#include <vr/FurDynamics.h>
#include <vr/FramebufferImage.h>
#include <vr/GeometryFactory.h>
#include <vr/Reader.h>
#include <vr/RenderVisitor.h>
#include <vr/Subdivision.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace{
unsigned checks=0;
void check(bool ok,const char* message){++checks;if(!ok) throw std::runtime_error(message);}

void dynamics(){
    auto a=std::make_shared<vr::Transform>(),b=std::make_shared<vr::Transform>();
    vr::FurDanceAnimation first(a,1,.3),second(b,1,.3);
    for(int i=0;i<180;++i)first.update(*a,{0,1.0/60,true});
    for(int i=0;i<432;++i)second.update(*b,{0,1.0/144,true});
    check(glm::distance(first.spring().displacement,second.spring().displacement)<1e-5f,
          "Fixed-step fur physics agrees at 60 and 144 render frames per second");
    const auto before=first.spring().displacement;const auto pose=a->getMatrix();
    first.update(*a,{4,1,false});
    check(a->getMatrix()==pose && first.spring().displacement==before && !first.changesRenderedContent(),
          "Global animation pause freezes both the articulated pose and fur spring");
    first.update(*a,{5,1000,true});
    check(std::isfinite(first.spring().displacement.x) && glm::length(first.spring().displacement)<=.120001f,
          "A stalled frame cannot make the bounded spring diverge or escape its culling envelope");
    check(glm::length(before)>.001f,"Dancing accelerations generate visible nonzero inertial fur motion");
    vr::FurSpring settling;settling.displacement={.1f,0,0};
    for(int i=0;i<1200;++i)settling.step(glm::vec3(0),1.0f/240);
    check(glm::length(settling.displacement)<1e-5f && glm::length(settling.velocity)<1e-5f,
          "Damping dissipates energy after the driving force stops");
}

void displacement(const std::filesystem::path& root){
    auto mesh=vr::GeometryFactory::createSphere(12,8);
    auto state=std::make_shared<vr::State>();state->setUniform("uFurLength",.2f);
    const glm::vec3 bend(.1f,-.05f,.02f);state->setUniform("uFurBend",bend);mesh->setState(state);
    state->setUniform("uFurSoft",true);
    vr::prepareFurBounds(*mesh);
    auto refined=vr::Subdivision::create(mesh);
    auto refinedMesh=std::dynamic_pointer_cast<vr::Geometry>(
        std::dynamic_pointer_cast<vr::Group>(refined)->getChildren().front());
    check(refinedMesh && refinedMesh->getFurPadding()==mesh->getFurPadding(),
          "Subdivision retains the fur envelope");
    const auto revision=mesh->getDataRevision();
    vr::Program program(root/"shaders/phong-shading.vs",root/"shaders/phong-shading.fs",{},
                         {"worldPosition","furPosition","furLayer"});
    const auto model=glm::translate(glm::mat4(1),{1,2,3})*glm::scale(glm::mat4(1),{-2,.7f,1.3f});
    for(unsigned layers:{1u,14u,32u}){
        const auto count=mesh->getIndices().size()*layers;
        GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
        glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,count*7*sizeof(float),nullptr,GL_STREAM_READ);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);
        program.use();program.set("uInstanced",false);program.set("uFurPass",true);
        program.set("uFurLength",.2f);program.set("uFurLayers",static_cast<int>(layers));
        program.set("uFurBend",bend);program.set("uModel",model);
        const glm::vec3 motion(.11f,-.03f,.01f);
        program.set("uFurSoft",layers==32);program.set("uFurMotion",motion);
        program.set("uNormal",glm::transpose(glm::inverse(glm::mat3(model))));
        program.set("uView",glm::mat4(1));program.set("uProjection",glm::mat4(1));
        glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_TRIANGLES);
        mesh->drawRepeated(layers);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
        std::vector<float> values(count*7);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,values.size()*sizeof(float),values.data());
        for(std::size_t i=0;i<count;++i){
            const auto& vertex=mesh->getVertices()[mesh->getIndices()[i%mesh->getIndices().size()]];
            const auto h=(i/mesh->getIndices().size()+.5f)/layers;
            const auto normal=glm::normalize(vertex.normal);
            const auto inertia=layers==32?motion-normal*glm::dot(normal,motion):glm::vec3(0);
            const auto p=vertex.position+normal*(.2f*h)+(bend+inertia)*h*h;
            const glm::vec3 actual(values[i*7],values[i*7+1],values[i*7+2]);
            check(glm::distance(actual,glm::vec3(model*glm::vec4(p,1)))<2e-5f,
                  "GPU shell offsets match the local normal and bend under mirrored/nonuniform transforms");
            check(glm::distance(glm::vec3(values[i*7+3],values[i*7+4],values[i*7+5]),vertex.position)<1e-6f &&
                      std::abs(values[i*7+6]-h)<1e-6f,
                  "Every shell retains stable root coordinates and its own layer height");
            check(glm::all(glm::greaterThanEqual(p,mesh->getBound().getMinimum())) &&
                      glm::all(glm::lessThanEqual(p,mesh->getBound().getMaximum())),
                  "The conservative scene-graph bound contains every shell");
        }
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);glDeleteBuffers(1,&buffer);
    }
    check(mesh->getDataRevision()==revision,"Shell expansion never rewrites source geometry");
}

std::vector<unsigned char> pixels(){
    std::vector<unsigned char> data(256*256*4);
    glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,data.data());return data;
}

void blendingAndDepth(const std::filesystem::path& root){
    auto program=std::make_shared<vr::Program>(root/"shaders/phong-shading.vs",root/"shaders/phong-shading.fs");
    auto graph=std::make_shared<vr::Group>();auto state=std::make_shared<vr::State>();
    state->setLighting(false);state->setUniform("uFog",false);graph->setState(state);
    auto mesh=vr::GeometryFactory::createSphere(32,24);auto coat=std::make_shared<vr::State>();
    auto material=std::make_shared<vr::Material>();material->setDiffuse({.2f,.1f,.04f});coat->setMaterial(material);
    coat->setUniform("uFurLength",.2f);coat->setUniform("uFurLayers",14);
    coat->setTexture(2,std::make_shared<vr::Texture>(1,1,std::vector<unsigned char>{200,200,200,255}));
    mesh->setState(coat);graph->addChild(mesh);vr::prepareFurBounds(*graph);
    vr::Camera camera;camera.setPerspective(50,1,.1f,20);camera.setLookAt({0,0,4},{0,0,0});
    vr::RenderVisitor renderer(program);
    const auto render=[&](bool fur){
        state->setUniform("uFurRendering",fur);glViewport(0,0,256,256);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);glClearColor(0,0,0,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        renderer.render(*graph,camera);return pixels();
    };
    const auto plain=render(false);const auto coatImage=render(true);
    check(renderer.getStats().draws==2 && renderer.getStats().instancedDraws==1 &&
              renderer.getStats().triangles==mesh->getTriangleCount()*15,
          "Fourteen shells reuse the mesh in one extra instanced submission");
    unsigned silhouette=0;
    for(std::size_t i=0;i<plain.size();i+=4){
        if(plain[i]==0 && plain[i+1]==0 && plain[i+2]==0 && coatImage[i]>5) ++silhouette;
        check(coatImage[i]>=plain[i] && coatImage[i+1]>=plain[i+1] && coatImage[i+2]>=plain[i+2],
              "Additive SRC_ALPHA + ONE blending only adds covered shell radiance");
    }
    check(silhouette>100,"The fur changes the silhouette beyond the original mesh");
    GLboolean write=GL_FALSE;glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
    check(write==GL_TRUE && !glIsEnabled(GL_BLEND),"The fur pass restores depth writes and disables blending");
    auto occluder=std::make_shared<vr::Geometry>();
    occluder->setData({{0,-3,1.5f},{3,-3,1.5f},{3,3,1.5f},{0,3,1.5f}}, {0,1,2,0,2,3},
                      std::vector<glm::vec3>(4,{0,0,1}));
    auto black=std::make_shared<vr::State>();auto blackMaterial=std::make_shared<vr::Material>();
    blackMaterial->setDiffuse({0,0,0});black->setMaterial(blackMaterial);black->setCullFace(false);
    occluder->setState(black);graph->addChild(occluder);
    const auto blocked=render(true);
    const auto blockedPlain=render(false);
    for(unsigned y=0;y<256;++y) for(unsigned x=130;x<256;++x){
        const auto i=(y*256+x)*4;
        check(blocked[i]==blockedPlain[i] && blocked[i+1]==blockedPlain[i+1] && blocked[i+2]==blockedPlain[i+2],
              "Opaque scene depth occludes the additive fur shells");
    }
    coat->setUniform("uFurSoft",true);
    vr::prepareFurBounds(*graph);
    vr::DeferredRenderer hdr(root);hdr.resize(256,256);hdr.setPreviews(false);
    state->setUniform("uFurRendering",true);
    renderer.render(*graph,camera,nullptr,nullptr,&hdr);
    check(renderer.getStats().screenCopies==1 && glGetError()==GL_NO_ERROR,
          "Groomed fur snapshots root lighting once into a separate texture without framebuffer feedback");
}

void application(const std::filesystem::path& root,GLFWwindow* window){
    const auto file=std::filesystem::temp_directory_path()/("acg-fur-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".xml");
    struct Remove{std::filesystem::path p;~Remove(){std::error_code e;std::filesystem::remove(p,e);}} cleanup{file};
    std::ofstream(file)<<R"(<scene><camera eye="0 0 4" target="0 0 0"/>
<group><state furLength=".18" furLayers="14" furDensity="4" diffuse=".2 .1 .04" lighting="false"
texture2="experiments/pantheon/textures/fur-roots.png"/><instance ref="material-sphere" name="Fur fixture"/></group></scene>)";
    vr::Application app(window,root,file);app.resize(256,256);app.handleKey(GLFW_KEY_TAB);app.update(0,0);
    for(unsigned path=0;path<2;++path){
        for(unsigned aa=0;aa<3;++aa){
            app.render();auto on=vr::FramebufferImage::capture(0,0,256,256);
            app.handleKey(GLFW_KEY_F11);app.render();auto off=vr::FramebufferImage::capture(0,0,256,256);
            check(on.countChangedPixels(off)>100,"F11 toggles visible fur in forward/deferred and all AA modes");
            app.handleKey(GLFW_KEY_F11);app.render();
            const auto restored=vr::FramebufferImage::capture(0,0,256,256);
            // MSAA resolve can round a half-float tie to adjacent 8-bit values.
            check(on.countChangedPixels(restored,1)==0,
                  "Re-enabling fur reproduces the same stable root pattern");
            app.handleKey(GLFW_KEY_F3);
        }
        app.handleKey(GLFW_KEY_F2);
    }
    app.handleKey(GLFW_KEY_F11);app.render();
    const auto beforeReload=vr::FramebufferImage::capture(0,0,256,256);
    app.reload();app.update(0,0);app.render();
    check(beforeReload.countChangedPixels(vr::FramebufferImage::capture(0,0,256,256))==0,
          "Reload preserves the selected fur comparison setting");
    for(const auto& value:{"furLength=\"-1\"","furLength=\"nan\"","furLayers=\"2.5\"","furLayers=\"65\""}){
        std::ofstream(file)<<"<scene><group><state "<<value<<"/></group></scene>";
        bool rejected=false;try{vr::Reader reader(root);reader.readScene(file);}catch(const std::exception&){rejected=true;}
        check(rejected,"Malformed fur settings are rejected before rendering");
    }
}
}

int main(int argc,char** argv){
    if(argc!=2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(256,256,"Shell fur tests",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);int result=0;
    try{
        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) throw std::runtime_error("OpenGL failed");
        dynamics();displacement(argv[1]);blendingAndDepth(argv[1]);application(argv[1],window);
        check(glGetError()==GL_NO_ERROR,"Fur paths leave valid OpenGL state");
        std::cout<<"Shell fur: "<<checks<<" checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
