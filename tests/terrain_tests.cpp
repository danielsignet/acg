#include <vr/Geometry.h>
#include <vr/Program.h>
#include <vr/CullVisitor.h>
#include <vr/Camera.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace{
unsigned checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void verify(const std::filesystem::path& root){
    std::vector<unsigned char> rgba;
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
        rgba.insert(rgba.end(),{128,128,255,static_cast<unsigned char>((x+2*y)*255/9)});
    auto height=std::make_shared<vr::Texture>(4,4,rgba,false);
    auto mesh=std::make_shared<vr::Geometry>("height-map test");
    mesh->setHeightField(height,{8,3,6},8,6);
    check(mesh->getTriangleCount()==96,"One pair of triangles per height-field cell");
    check(mesh->getBound().getMaximum().y>2.9f,"Bounds include displaced heights");
    auto hit=mesh->intersectRay({0,10,0},{0,-1,0},20);
    check(hit && std::abs(*hit-(10-3*height->sampleAlpha({.5f,.5f})))<1e-4f,
        "Picking hits the height-field surface instead of its base plane");
    bool rejected=false;try{mesh->setHeightField(height,{8,3,6},513,6);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Unbounded terrain amplification is rejected");
    const auto count=mesh->getIndices().size();
    const auto model=glm::rotate(glm::translate(glm::mat4(1),{2,1,-3}),.37f,glm::vec3(0,1,0))*glm::scale(glm::mat4(1),{1.2f,.8f,1.7f});
    const auto normal=glm::transpose(glm::inverse(glm::mat3(model)));
    vr::Program color(root/"shaders/terrain.vs",root/"shaders/phong-shading.fs",root/"shaders/terrain.gs",{"worldPosition","worldNormal"});
    vr::Program depth(root/"shaders/terrain.vs",root/"shaders/shadow-depth.fs",root/"shaders/terrain-depth.gs",{"shadowWorld"});
    GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,count*6*sizeof(float),nullptr,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);
    const auto capture=[&](vr::Program& program,bool shadow){
        program.use();mesh->applyHeightField(program);program.set("uModel",model);program.set("uNormal",normal);
        program.set("uView",glm::mat4(1));program.set("uProjection",glm::mat4(1));program.set("uLightVP",glm::mat4(1));
        glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_TRIANGLES);mesh->draw();glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
        std::vector<float> actual(count*(shadow?3:6));
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,actual.size()*sizeof(float),actual.data());return actual;
    };
    auto actual=capture(color,false);auto shadow=capture(depth,true);
    for(std::size_t i=0;i<count;++i){
        const auto& vertex=mesh->getVertices()[mesh->getIndices()[i]];
        const auto expected=glm::vec3(model*glm::vec4(vertex.position,1));
        const glm::vec3 p(actual[i*6],actual[i*6+1],actual[i*6+2]);
        if(glm::distance(expected,p)>=3e-5f)std::cerr<<"Vertex "<<i<<" uv "<<vertex.texCoord.x<<','<<vertex.texCoord.y
            <<" expected "<<expected.x<<','<<expected.y<<','<<expected.z<<" actual "<<p.x<<','<<p.y<<','<<p.z<<'\n';
        check(glm::distance(expected,p)<3e-5f,"Geometry shader positions match exact CPU picking geometry");
        check(glm::distance(p,{shadow[i*3],shadow[i*3+1],shadow[i*3+2]})<3e-5f,"Shadow and color geometry coincide");
        check(glm::dot(glm::normalize(normal*vertex.normal),glm::vec3(actual[i*6+3],actual[i*6+4],actual[i*6+5]))>.9999f,
            "Height-gradient normals survive nonuniform transforms");
    }
    // Flatten every source vertex: the geometry stage must still reconstruct height from the image.
    std::vector<glm::vec3> flat,normals;std::vector<glm::vec2> uv;
    for(const auto& v:mesh->getVertices()){flat.push_back({v.position.x,0,v.position.z});normals.push_back(v.normal);uv.push_back(v.texCoord);}
    mesh->setData(flat,mesh->getIndices(),normals,uv);
    auto reconstructed=capture(color,false);
    check(reconstructed==actual,"Geometry stage actually reads height map, independently of CPU input Y");
    auto base=std::make_shared<vr::Program>(root/"shaders/phong-shading.vs",root/"shaders/phong-shading.fs");
    check(base->heightFieldVariant()==base->heightFieldVariant(),"Compiled terrain variants are cached");
    vr::Program gbuffer(root/"shaders/phong-shading.vs",root/"shaders/gbuffer.fs");
    check(gbuffer.heightFieldVariant()->getId()!=0,"Terrain links with deferred G-buffer fragment stage");
    check(glGetError()==GL_NO_ERROR,"Terrain draws leave no OpenGL errors");glDeleteBuffers(1,&buffer);
}
}
int main(int argc,char** argv){
    GLFWwindow* window=nullptr;
    try{
        if(argc!=2 || !glfwInit())throw std::runtime_error("Missing root or GLFW");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);window=glfwCreateWindow(64,64,"Terrain",nullptr,nullptr);
        if(!window)throw std::runtime_error("No OpenGL 4.1 context");glfwMakeContextCurrent(window);
        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))throw std::runtime_error("No GL loader");
        verify(argv[1]);glfwDestroyWindow(window);glfwTerminate();std::cout<<checks<<" terrain checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}
}
