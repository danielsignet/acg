#include <vr/SkyParticles.h>
#include <vr/EnvironmentMap.h>
#include <vr/FramebufferImage.h>
#include <vr/GLState.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace{
unsigned checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
std::vector<unsigned char> pixels(){std::vector<unsigned char> p(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
void gpuStream(const std::filesystem::path& root){
    const auto file=root/"experiments/pantheon/textures/astra-particles.bin";
    vr::SkyParticles field(file);check(field.count()==131072 && field.bytes()==4194304,"One 4 MiB buffer stores all particle populations");
    GLint buffer=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
    std::vector<float> before(field.count()*8);glGetBufferSubData(GL_ARRAY_BUFFER,0,before.size()*4,before.data());
    std::ifstream in(file,std::ios::binary);in.seekg(16);unsigned groups=0;in.read(reinterpret_cast<char*>(&groups),4);
    std::vector<glm::mat4> formations(groups);float speed;
    for(auto& matrix:formations){in.read(reinterpret_cast<char*>(&matrix),64);in.read(reinterpret_cast<char*>(&speed),4);}
    vr::Program program(root/"shaders/sky-particles.vs",root/"shaders/sky-particles.fs",{}, {"gl_Position","particleColor"});
    GLuint feedback;glGenBuffers(1,&feedback);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,field.count()*7*4,nullptr,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
    vr::Camera camera;camera.setPerspective(48,1,.1f,100);camera.setLookAt({0,0,0},{0,0,-1});
    program.use();glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_POINTS);
    field.draw(program,camera,0,800);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
    std::vector<float> values(field.count()*7);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,values.size()*4,values.data());
    for(unsigned i=0;i<field.count();i+=127){
        const auto* p=&before[i*8];const unsigned group=static_cast<unsigned>(p[7]/8);
        if(group==7)continue; // Meteor positions are tested for motion below.
        const auto direction=glm::normalize(glm::vec3(formations[group]*glm::vec4(p[0],p[1],p[2],1)));
        auto expected=camera.getProjection()*glm::vec4(direction,0);expected.z=expected.w;
        const glm::vec4 actual(values[i*7],values[i*7+1],values[i*7+2],values[i*7+3]);
        check(glm::length(actual-expected)<2e-5f,"Actual point vertices match catalog formations and remain at the far plane");
        check(std::isfinite(values[i*7+4]) && values[i*7+4]>=0,"Particle radiance remains finite");
    }
    glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_POINTS);
    field.draw(program,camera,.2f,800);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
    std::vector<float> moved(values.size());glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,moved.size()*4,moved.data());
    unsigned moving=0;
    for(unsigned i=field.count()-192;i<field.count();++i)
        moving+=std::abs(moved[i*7]-values[i*7])+std::abs(moved[i*7+1]-values[i*7+1])>.001f;
    check(moving>150,"Meteor and trail particle positions visibly move on the GPU");
    glBindBuffer(GL_ARRAY_BUFFER,buffer);std::vector<float> after(before.size());
    glGetBufferSubData(GL_ARRAY_BUFFER,0,after.size()*4,after.data());
    check(before==after,"Drawing and animating formations never rewrite the particle buffer");
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);glDeleteBuffers(1,&feedback);
}
void images(const std::filesystem::path& root){
    vr::SceneEnvironments layout;
    layout.build({{"stars","experiments/pantheon/textures/astra-stars.hdr",1,"experiments/pantheon/textures/astra-particles.bin"}},
        {{"room",1,{{-2,-2,-6},{2,2,-2}},{0,0,0,0},{{-.65f,-.65f,-2},{.65f,.65f,-2}}}});
    vr::EnvironmentMap environment(root,"textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr");
    environment.configureRooms(root,layout);check(environment.getParticleBytes()==4194304,"Environment owns only one particle stream");
    vr::Camera camera;camera.setPerspective(48,1,.1f,100);camera.setLookAt({0,0,-4},{0,1,-5});
    const auto draw=[&](bool enabled){
        glViewport(0,0,256,256);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        environment.setParticlesEnabled(enabled);environment.drawSky(camera,false);return pixels();
    };
    const auto off=draw(false),on=draw(true);unsigned changed=0;
    for(unsigned i=0;i<on.size();i+=4)changed+=on[i]!=off[i] || on[i+1]!=off[i+1] || on[i+2]!=off[i+2];
    check(changed>1000 && environment.getParticleDraws()==1 && environment.getParticleCount()==131072,
          "The displayed sky is a live single-draw particle system, with visible Milky Way and galaxy populations");
    environment.advanceParticles(2,false);check(draw(true)==on,"Frozen scene time freezes the entire particle sky exactly");
    environment.advanceParticles(2,true);check(draw(true)!=on,"Particles animate without any buffer upload");
    camera.setLookAt({.25f,0,-4},{.25f,1,-5});const auto translated=draw(true);
    camera.setLookAt({0,0,-4},{0,1,-5});check(draw(true)==translated,"Sky particles have no camera-translation parallax");

    camera.setLookAt({0,0,0},{0,0,-1});const auto outsideOff=draw(false),outsideOn=draw(true);
    const auto inverseProjection=glm::inverse(camera.getProjection()),inverseView=glm::inverse(camera.getView());
    unsigned doorway=0;
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){
        const unsigned i=(y*256+x)*4;
        const bool different=outsideOff[i]!=outsideOn[i] || outsideOff[i+1]!=outsideOn[i+1] || outsideOff[i+2]!=outsideOn[i+2];
        if(!different)continue;
        const auto ray=glm::mat3(inverseView)*glm::vec3(inverseProjection*glm::vec4((x+.5f)/128-1,(y+.5f)/128-1,1,1));
        check(layout.skyForRay(camera.getPosition(),ray)==1,"Every star fragment respects the actual room opening");++doorway;
    }
    check(doorway>20,"Stars remain visible through the room opening");
    camera.setLookAt({0,0,0},{0,0,1});draw(true);
    check(environment.getParticleDraws()==0,"A sky outside the observer frustum submits zero particle draws");

    camera.setLookAt({0,0,-4},{0,1,-5});environment.setParticlesEnabled(true);
    for(int samples:{1,4}){
        vr::GBuffer geometry(256,256,samples);vr::ColorTarget target(256,256,geometry.getDepth()),resolved(256,256);
        const auto deferred=[&](bool blocked){
            geometry.begin();
            if(blocked){glEnable(GL_SCISSOR_TEST);glScissor(128,0,128,256);glClearDepth(.5);glClear(GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);glClearDepth(1);}
            target.bind();glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            environment.drawDeferred(camera,geometry,geometry.get(vr::GBuffer::Ambient),0);
            if(samples>1){target.resolveTo(resolved);resolved.bind();}
            return pixels();
        };
        const auto open=deferred(false),blocked=deferred(true);unsigned hidden=0;
        for(unsigned y=0;y<256;++y)for(unsigned x=140;x<256;++x){
            const auto i=(y*256+x)*4;hidden+=open[i]!=blocked[i] || open[i+1]!=blocked[i+1] || open[i+2]!=blocked[i+2];
            check(blocked[i]==0 && blocked[i+1]==0 && blocked[i+2]==0,"Opaque depth occludes all star fragments, including MSAA");
        }
        check(hidden>100,"Deferred and multisample pipelines actually draw particles behind scene depth");
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,256,256);
}
}
int main(int argc,char** argv){
 if(argc!=2 || !glfwInit())return 1;
 glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
 glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);auto* window=glfwCreateWindow(256,256,"Star particle tests",nullptr,nullptr);
 if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);int result=0;
 try{if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))throw std::runtime_error("OpenGL failed");
     gpuStream(argv[1]);images(argv[1]);check(glGetError()==GL_NO_ERROR,"Particle rendering leaves valid OpenGL state");
     std::cout<<"Star particles: "<<checks<<" checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
 glfwDestroyWindow(window);glfwTerminate();return result;
}
