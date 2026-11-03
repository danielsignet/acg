#include <vr/TreeDeformation.h>
#include <vr/Camera.h>
#include <vr/CullVisitor.h>
#include <vr/Light.h>
#include <vr/Scene.h>
#include <vr/ShadowRenderer.h>
#include <vr/Transform.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace{
unsigned checks=0;void check(bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);}
const glm::vec4 warps[]={{-1.35f,.35f,.85f,-.18f},{.6f,-.9f,-.6f,.4f},{1.8f,.75f,1.15f,-.1f},
                        {-.7f,-.5f,-.95f,.2f},{.35f,1.45f,.55f,-.28f},{-1.65f,.2f,1.35f,.32f}};
std::shared_ptr<vr::Geometry> trunk(){
    auto mesh=std::make_shared<vr::Geometry>();std::vector<glm::vec3> p,n;std::vector<glm::vec2> uv;std::vector<unsigned> indices;
    for(unsigned y=0;y<9;++y)for(unsigned a=0;a<16;++a){
        float t=a*6.2831853f/16;p.push_back({.3f*std::cos(t),float(y),.3f*std::sin(t)});
        n.push_back({std::cos(t),0,std::sin(t)});uv.push_back({a/16.f,y/8.f});
    }
    for(unsigned y=0;y<8;++y)for(unsigned a=0;a<16;++a){unsigned i=y*16+a,j=y*16+(a+1)%16;
        indices.insert(indices.end(),{i,j,i+16,j,j+16,i+16});}
    mesh->setData(p,indices,n,uv);return mesh;
}
void gpu(const std::filesystem::path& root){
    auto mesh=trunk();const auto revision=mesh->getDataRevision();
    vr::Program color(root/"shaders/phong-shading.vs",root/"shaders/phong-shading.fs",{}, {"worldPosition","worldNormal"});
    vr::Program shadow(root/"shaders/shadow-depth.vs",root/"shaders/shadow-depth.fs",{}, {"shadowWorld"});
    const auto count=mesh->getIndices().size();GLuint feedback;glGenBuffers(1,&feedback);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,count*6*4,nullptr,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
    for(const auto& warp:warps){
        vr::TreeShape shape{8,warp};color.use();color.set("uTreeHeight",8.f);color.set("uTreeWarp",warp);
        color.set("uModel",glm::mat4(1));color.set("uNormal",glm::mat3(1));color.set("uView",glm::mat4(1));color.set("uProjection",glm::mat4(1));
        glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_TRIANGLES);mesh->draw();glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
        std::vector<float> actual(count*6);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,actual.size()*4,actual.data());
        for(std::size_t i=0;i<count;i+=7){
            const auto& v=mesh->getVertices()[mesh->getIndices()[i]];const auto expected=vr::treePoint(v.position,shape);
            check(glm::distance(expected,{actual[i*6],actual[i*6+1],actual[i*6+2]})<2e-5f,"Shared GPU vertices receive each instance's nonlinear bend, twist and spread");
            if(v.position.y>.01f){
                constexpr float e=.001f;glm::mat3 jacobian;
                for(unsigned a=0;a<3;++a){glm::vec3 d(0);d[a]=e;jacobian[a]=(vr::treePoint(v.position+d,shape)-vr::treePoint(v.position-d,shape))/(2*e);}
                const auto normal=glm::normalize(glm::transpose(glm::inverse(jacobian))*v.normal);
                check(glm::dot(normal,glm::vec3(actual[i*6+3],actual[i*6+4],actual[i*6+5]))>.9998f,
                      "GPU normals match an independent finite-difference deformation Jacobian");
            }
        }
        shadow.use();shadow.set("uTreeHeight",8.f);shadow.set("uTreeWarp",warp);shadow.set("uModel",glm::mat4(1));shadow.set("uLightVP",glm::mat4(1));
        glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_TRIANGLES);mesh->draw();glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
        std::vector<float> depth(count*3);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,depth.size()*4,depth.data());
        for(std::size_t i=0;i<count;i+=7)check(glm::distance(glm::vec3(depth[i*3],depth[i*3+1],depth[i*3+2]),
            glm::vec3(actual[i*6],actual[i*6+1],actual[i*6+2]))<2e-5f,"Shadow and color passes use identical deformed positions");
        check(vr::treePoint({0,0,0},shape)==glm::vec3(0),"Root anchors do not move under crown deformation");
        for(float y=0;y<8;y+=.25f){
            const vr::BoundingBox box({-.8f,y,-.5f},{.4f,y+.2f,.7f});const auto bound=vr::treeBound(box,shape);
            for(const auto& corner:box.corners()){
                const auto p=vr::treePoint(corner,shape);
                check(glm::all(glm::greaterThanEqual(p,bound.getMinimum()-glm::vec3(1e-4f))) &&
                      glm::all(glm::lessThanEqual(p,bound.getMaximum()+glm::vec3(1e-4f))),"Deformed BVH volumes enclose every tested corner");
            }
        }
    }
    check(mesh->getDataRevision()==revision,"Six different shapes retain the same immutable source geometry");
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);glDeleteBuffers(1,&feedback);
}
void graphAndPicking(){
    auto root=std::make_shared<vr::Group>();auto mesh=trunk();
    for(const auto& warp:warps){auto placement=std::make_shared<vr::Transform>();auto state=std::make_shared<vr::State>();
        state->setUniform("uTreeHeight",8.f);state->setUniform("uTreeWarp",warp);placement->setState(state);placement->addChild(mesh);root->addChild(placement);}
    vr::prepareTreeBounds(*root);vr::CullVisitor culler;culler.collect(*root,glm::mat4(1),{0,4,10},false);
    check(culler.getRenderList().size()==6,"Six graph placements retain six independently inherited deformation states");
    for(unsigned i=0;i<6;++i)check(culler.getRenderList()[i].geometry==mesh.get() &&
        vr::treeShape(culler.getRenderList()[i].state).warp==warps[i],"All placements share exactly the same mesh and inherit their own shape");
    auto panel=std::make_shared<vr::Geometry>();panel->setData({{-.1f,0,0},{.1f,0,0},{.1f,8,0},{-.1f,8,0}},{0,1,2,0,2,3},
        {{0,0,1},{0,0,1},{0,0,1},{0,0,1}});
    panel->setMotionPadding({4,0,4});const vr::TreeShape shape{8,{2,0,0,0}};
    const glm::vec3 eye(1.975f,7.9f,10),ray(0,0,-1);
    check(!panel->intersectRay(eye,ray,100),"Reference rest geometry misses the visibly bent crown");
    const auto hit=panel->intersectRay(eye,ray,100,{},[shape](const auto& p){return vr::treePoint(p,shape);},
        [shape](const auto& box){return vr::treeBound(box,shape);});
    check(hit && std::abs(*hit-10)<1e-4f,"Picking intersects the deformed triangles outside the original BVH");
}
void cache(const std::filesystem::path& root){
    vr::Scene scene;auto mesh=trunk();mesh->setMotionPadding({6,0,6});
    auto state=std::make_shared<vr::State>();state->setCastShadows(true);state->setUniform("uTreeHeight",8.f);state->setUniform("uTreeWarp",warps[0]);
    mesh->setState(state);scene.getRoot()->addChild(mesh);auto sun=std::make_shared<vr::Light>();sun->setPosition({0,1,1,0});scene.getRoot()->addChild(sun);
    vr::ShadowRenderer renderer(root,128,64);renderer.render(scene,{0,4,10},true,true);check(renderer.getStats().draws>0,"Deformed trees cast actual light-space depth");
    renderer.render(scene,{0,4,10},true,true);check(renderer.getStats().draws==0,"An unchanged deformed tree reuses its shadow depth");
    state->setUniform("uTreeWarp",warps[1]);renderer.render(scene,{0,4,10},true,true);
    check(renderer.getStats().draws>0,"Changing shape invalidates static shadow cache even when bounds and model matrices stay unchanged");
}
}
int main(int argc,char** argv){
 if(argc!=2 || !glfwInit())return 1;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
 glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
 auto* window=glfwCreateWindow(256,256,"Tree deformation tests",nullptr,nullptr);if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);int result=0;
 try{if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))throw std::runtime_error("OpenGL failed");
     gpu(argv[1]);graphAndPicking();cache(argv[1]);check(glGetError()==GL_NO_ERROR,"Tree deformation leaves valid OpenGL state");std::cout<<"Tree deformation: "<<checks<<" checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}glfwDestroyWindow(window);glfwTerminate();return result;
}
