#include <vr/EnvironmentMap.h>
#include <vr/FramebufferImage.h>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/Reader.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>

namespace{
unsigned checks=0;
void check(bool condition,const char* message){
    ++checks;if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action,const char* message){
    bool rejected=false;
    try{action();}catch(const std::exception&){rejected=true;}
    check(rejected,message);
}
vr::SceneEnvironments layout(const std::filesystem::path& red){
    vr::SceneEnvironments result;
    result.build({{"red",red,1}},{{"room",1,{{-2,-2,-6},{2,2,-2}},{0,0,0,0},
                                  {{-.65f,-.65f,-2},{.65f,.65f,-2}}}});
    return result;
}
void writeHDR(const std::filesystem::path& path,bool red){
    std::ofstream out(path,std::ios::binary);
    out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
    for(int i=0;i<32;++i){
        const unsigned char pixel[4]={static_cast<unsigned char>(red?120+i%8:0),0,
                                      static_cast<unsigned char>(red?0:128),129};
        out.write(reinterpret_cast<const char*>(pixel),4);
    }
}
void cpuTests(const std::filesystem::path& root,const std::filesystem::path& out){
    auto scene=layout(out/"red.hdr");
    check(scene.skyForRay({0,0,-4},{0,0,1})==0,"Inside room must see world sky through opening");
    check(scene.skyForRay({0,0,-4},{0,1,0})==1,"Room's open sky must use its own cubemap");
    check(scene.skyForRay({0,0,0},{0,0,-1})==1,"Outside observer must see room sky through opening");
    check(scene.skyForRay({2,0,0},{0,0,-1})==0,"Outside opening must retain world sky");
    check(scene.skyForRay({0,0,-2},{0,0,1})==0,"Opening boundary must work looking out");
    check(scene.skyForRay({0,0,-2},{0,0,-1})==1,"Opening boundary must work looking in");
    auto guarded=layout(out/"red.hdr");
    auto guardedRegions=guarded.getRegions();
    guardedRegions.push_back({"world-room",0,{{-2,-2,2},{2,4,6}},{0,0,0,0},{{-.65f,-.65f,2},{.65f,.65f,2}}});
    guarded.build(guarded.getSkies(),guardedRegions);
    check(guarded.skyForRay({0,3,4},{0,-3,-6})==0,
          "A distant star opening must not leak outside the observer room's own doorway");
    check(guarded.skyForRay({0,0,4},{0,0,-1})==1,
          "Stars remain visible when the ray passes through both doorways");
    auto regions=scene.getRegions();regions[0].cylinder={0,0,-4,2};
    scene.build(scene.getSkies(),regions);
    check(scene.skyAt({1.9f,0,-5.9f})==0 && scene.skyAt({0,0,-4})==1,"Cylinder must exclude room bounding-box corners");
    rejects([&]{auto bad=regions;bad[0].sky=2;scene.build(scene.getSkies(),bad);},"Unknown sky layer must be rejected");
    rejects([&]{auto bad=regions;bad[0].opening={{-.65f,-.65f,-3},{.65f,.65f,-3}};
        scene.build(scene.getSkies(),bad);},"An opening must lie on the room boundary");
    const auto xml=out/"sky-layout.xml";
    {
        std::ofstream file(xml);
        file << "<scene><environments><sky id=\"stars\" file=\"red.hdr\" exposure=\"1\"/>"
                "<region room=\"room\" sky=\"stars\" min=\"-2 -2 -6\" max=\"2 2 -2\">"
                "<opening min=\"-.65 -.65 -2\" max=\".65 .65 -2\"/>"
                "</region></environments></scene>";
    }
    vr::Reader reader(root);
    const auto loaded=reader.readScene(xml);
    check(loaded->getEnvironments().skyForRay({0,0,-4},{0,0,1})==0 &&
          loaded->getEnvironments().skyForRay({0,0,-4},{0,0,-1})==1,
          "XML must preserve the environment layout independently of visibility culling");
}

constexpr int Width=128,Height=96;
std::vector<unsigned char> readPixels(){
    std::vector<unsigned char> pixels(Width*Height*3);
    glReadPixels(0,0,Width,Height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    return pixels;
}

void skyPass(vr::EnvironmentMap& environment,const vr::SceneEnvironments& scene,
             const glm::vec3& eye,const glm::vec3& direction,const std::filesystem::path& file){
    vr::Camera camera;
    camera.setPerspective(70,static_cast<float>(Width)/Height,.1f,100);
    camera.setLookAt(eye,eye+direction);
    environment.drawSky(camera,true);
    const auto pixels=readPixels();
    const auto inverseProjection=glm::inverse(camera.getProjection());
    const auto inverseView=glm::inverse(camera.getView());
    unsigned red=0,blue=0,mismatches=0;
    for(int y=0;y<Height;++y) for(int x=0;x<Width;++x){
        const auto projected=inverseProjection*glm::vec4((x+.5f)/Width*2-1,(y+.5f)/Height*2-1,1,1);
        const auto ray=glm::mat3(inverseView)*glm::vec3(projected);
        const unsigned layer=scene.skyForRay(eye,ray);
        const auto index=(y*Width+x)*3;
        const bool isRed=pixels[index]>230 && pixels[index+1]<3 && pixels[index+2]<3;
        const bool isBlue=pixels[index]<3 && pixels[index+1]<3 && pixels[index+2]>250;
        red+=isRed;blue+=isBlue;
        mismatches+=(layer==1 ? !isRed : !isBlue);
    }
    check(red>100 && blue>100,"Both room and world cubemaps must be visible in the same frame");
    check(mismatches==0,"GPU aperture mask must agree with independent CPU ray queries at every pixel");
    vr::FramebufferImage::capture(0,0,Width,Height).save(file);
    std::cout << file.filename().string() << ": " << red << " room pixels, " << blue
              << " world pixels, " << mismatches << " mask mismatches\n";
}

void iblTests(vr::EnvironmentMap& environment,const std::filesystem::path& root,const std::filesystem::path& out){
    const auto shader=out/"sky-ibl.fs";
    {
        std::ofstream file(shader);
        file << "#version 410 core\nout vec4 fragmentColor;\n"
             << "#include \"" << (root/"shaders/brdf.glsl").generic_string() << "\"\n"
             << "#include \"" << (root/"shaders/environment.glsl").generic_string() << "\"\n"
             << "uniform vec3 uSamplePosition,uSampleDirection; uniform int uProbe,uTestDiffuse;\n"
                "void main(){fragmentColor=vec4(uTestDiffuse==1 ? diffuseEnvironment(vec3(0,1,0),uSamplePosition) : "
                "environmentRadiance(uSampleDirection,0,uProbe,uSamplePosition),1);}\n";
    }
    vr::Program program(root/"shaders/screen-quad.vs",shader);
    vr::ScreenQuad quad;
    const auto sample=[&](const glm::vec3& point,const glm::vec3& ray,int probe,bool diffuse){
        program.use();environment.apply(program,0);
        program.set("uSamplePosition",point);program.set("uSampleDirection",ray);
        program.set("uProbe",probe);program.set("uTestDiffuse",diffuse?1:0);
        quad.draw();
        float pixel[3]{};
        glReadPixels(Width/2,Height/2,1,1,GL_RGB,GL_FLOAT,pixel);
        return glm::vec3(pixel[0],pixel[1],pixel[2]);
    };
    const auto red=sample({0,0,-4},{0,0,-1},0,false);
    const auto blue=sample({0,0,-4},{0,0,1},0,false);
    check(red.x>.9f && red.z<.01f && blue.x>.9f && blue.z<.01f,
          "Surface fallback must stay in its own environment instead of drawing a foreign sky rectangle");
    const auto diffuseRed=sample({0,0,-4},{0,0,-1},0,true);
    const auto diffuseBlue=sample({0,0,0},{0,0,-1},0,true);
    check(diffuseRed.x>0 && diffuseRed.z==0 && diffuseBlue.z>0 && diffuseBlue.x==0,
          "Diffuse SH must use the surface's room, not the camera's environment");
    environment.setReflectionMode(vr::ReflectionMode::Cubemaps);
    check(sample({0,0,-4},{0,0,-1},1,false).x>.9f,"Unready probes must fall back to the correct room sky");
    auto maps=vr::Texture::colorCubeArray(256,9,environment.getSkyCount()+1);
    environment.initializeLocalReflections(maps);
    auto green=vr::Texture::colorCube(4,3);
    {
        vr::FramebufferState saved;
        green->apply(0);
        GLuint framebuffer=0;glGenFramebuffers(1,&framebuffer);
        vr::GLState::bindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glClearColor(0,1,0,1);
        for(unsigned face=0;face<6;++face){
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,green->getId(),0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        vr::GLState::deleteFramebuffers(1,&framebuffer);
    }
    environment.prefilterLocal(green,maps,environment.getSkyCount());
    environment.setLocalReflections(maps,3);
    const auto probe=sample({0,0,-4},{0,0,-1},1,false);
    check(probe.y>.9f && probe.x<.01f && probe.z<.01f,
          "Published probes must use their layer after every room sky layer");
    check(sample({0,0,-4},{0,0,-1},0,false).x>.9f,
          "Publishing a probe must preserve the room's fallback cubemap");
}
}

int main(int argc,char** argv){
    GLFWwindow* window=nullptr;
    try{
        if(argc!=2) throw std::runtime_error("sky_environment_tests resource-root");
        const std::filesystem::path root=argv[1],out=root/"experiments/pantheon/output/sky-tests";
        std::filesystem::create_directories(out);
        writeHDR(out/"blue.hdr",false);writeHDR(out/"red.hdr",true);
        cpuTests(root,out);
        if(!glfwInit()) throw std::runtime_error("GLFW failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window=glfwCreateWindow(Width,Height,"Room sky tests",nullptr,nullptr);
        if(!window) throw std::runtime_error("Hidden context failed");
        glfwMakeContextCurrent(window);
        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            throw std::runtime_error("OpenGL failed");
        {
            vr::GLState::Scope tracked;
            vr::GLState::viewport(0,0,Width,Height);
            vr::EnvironmentMap environment(root,out/"blue.hdr",1);
            const auto scene=layout(out/"red.hdr");
            environment.configureRooms(root,scene);
            skyPass(environment,scene,{0,0,-4},{0,0,1},out/"inside-both.png");
            skyPass(environment,scene,{0,0,0},{0,0,-1},out/"outside-both.png");
            vr::Camera camera;
            camera.setPerspective(70,static_cast<float>(Width)/Height,.1f,100);
            camera.setLookAt({0,0,-4},{0,0,-5});environment.drawSky(camera,true);
            const auto before=vr::FramebufferImage::capture(0,0,Width,Height);
            const auto pattern=readPixels();
            std::set<unsigned char> values;
            for(std::size_t i=0;i<pattern.size();i+=3) values.insert(pattern[i]);
            check(values.size()>1,"Translation check must use a textured, nonconstant sky");
            camera.setLookAt({.2f,.1f,-4.2f},{.2f,.1f,-5.2f});environment.drawSky(camera,true);
            check(before.countChangedPixels(vr::FramebufferImage::capture(0,0,Width,Height),0)==0,
                  "Sky translation must not create finite-dome parallax");
            iblTests(environment,root,out);
            check(glGetError()==GL_NO_ERROR,"Environment passes must not leave OpenGL errors");
        }
        glfwDestroyWindow(window);glfwTerminate();
        std::cout << "Passed " << checks << " room-sky checks\n";return 0;
    }catch(const std::exception& error){
        std::cerr << error.what() << '\n';
        if(window) glfwDestroyWindow(window);glfwTerminate();return 1;
    }
}
