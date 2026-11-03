#include <vr/DeferredRenderer.h>
#include <vr/Reader.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace{
constexpr int width = 223, height = 157;
unsigned checks = 0;
void check(bool value, const char* message){
    ++checks;
    if (!value) throw std::runtime_error(message);
}

void packing(){
    auto heights = std::make_shared<vr::Texture>(2, 2,
        std::vector<unsigned char>{0,0,0,255, 64,0,0,255, 128,0,0,255, 255,0,0,255});
    auto normal = std::make_shared<vr::Texture>(1, 1,
        std::vector<unsigned char>{204,128,230,255});
    auto combined = heights->withNormal(normal);
    check(combined == heights->withNormal(normal), "Identical source pairs share one packed texture");
    combined->apply(4);
    std::vector<unsigned char> pixels(16);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (unsigned i = 0; i < 4; ++i){
        check(pixels[i*4] == 204 && pixels[i*4+1] == 128 && pixels[i*4+2] == 230 &&
                  pixels[i*4+3] == std::array<unsigned char,4>{0,64,128,255}[i],
              "Packing preserves normal RGB and resamples height into alpha");
    }
    check(normal->sampleAlpha({0.5f,0.5f}) == 1,
          "Relief packing does not replace the source normal/height assets");
    check(heights->withNormal(nullptr) != combined, "Height-only materials use their own cached image");
}

void importing(const std::filesystem::path& path){
    const auto file = std::filesystem::temp_directory_path() /
        ("acg-parallax-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".xml");
    struct Remove{ std::filesystem::path file; ~Remove(){ std::error_code error; std::filesystem::remove(file,error); } } remove{file};
    {
        std::ofstream xml(file);
        xml << R"(<scene><group><state texture0="textures/grid.png" normalMap="textures/brick-normal.png"
heightMap="textures/brick-height.png" parallax="true" parallaxScale=".04"/>
<instance ref="box"/><group><state parallax="false"/><instance ref="box"/></group></group></scene>)";
    }
    vr::Reader reader(path);
    reader.registerNode("box", vr::GeometryFactory::createBox());
    auto scene = reader.readScene(file);
    vr::CullVisitor collector;
    collector.collect(*scene->getRoot(), glm::mat4(1), {0,0,5}, false);
    const auto& items = collector.getRenderList();
    check(items.size() == 2 && items[0].state.getNormalMap() && items[0].state.getHeightMap() &&
              std::get<bool>(items[0].state.getUniforms().at("uParallax")) &&
              !std::get<bool>(items[1].state.getUniforms().at("uParallax")) &&
              std::abs(std::get<float>(items[0].state.getUniforms().at("uParallaxScale"))-.04f) < 1e-6f,
          "XML inherits matched maps and physical parallax scale, with a local disable override");
    {
        std::ofstream xml(file);
        xml << R"(<scene><group><state parallax="true" parallaxScale="2"/></group></scene>)";
    }
    bool rejected = false;
    try{ reader.readScene(file); } catch (const std::runtime_error&){ rejected = true; }
    check(rejected, "XML rejects excessive parallax depth");
}

void rendering(const std::filesystem::path& path){
    auto program = std::make_shared<vr::Program>(path / "shaders/phong-shading.vs",
                                                 path / "shaders/phong-shading.fs");
    vr::RenderVisitor renderer(program);
    renderer.setCulling(false);
    vr::DeferredRenderer deferred(path);
    deferred.resize(width,height);
    deferred.setPreviews(false);
    vr::Scene scene;
    auto state = std::make_shared<vr::State>();
    auto material = std::make_shared<vr::Material>();
    material->setDiffuse({1,1,1});
    material->setAmbient({.04f,.04f,.04f});
    state->setMaterial(material);
    state->setUniform("uFog",false);
    state->setUniform("uParallax",true);
    state->setUniform("uParallaxScale",.4f);
    scene.getRoot()->setState(state);
    std::vector<unsigned char> colors(256*256*4), normals(colors.size());
    for (unsigned y=0; y<256; ++y) for (unsigned x=0; x<256; ++x){
        const auto i=(y*256+x)*4;
        colors[i]=static_cast<unsigned char>(x); colors[i+1]=static_cast<unsigned char>(y);
        colors[i+2]=128; colors[i+3]=255;
        normals[i]=static_cast<unsigned char>(128+x/5); normals[i+1]=128;
        normals[i+2]=255; normals[i+3]=255;
    }
    state->setTexture(0,std::make_shared<vr::Texture>(256,256,std::move(colors),false));
    state->setNormalMap(std::make_shared<vr::Texture>(256,256,std::move(normals),false));
    state->setHeightMap(std::make_shared<vr::Texture>(1,1,std::vector<unsigned char>{64,64,64,255},false));
    auto mesh=std::make_shared<vr::Geometry>();
    const std::vector<glm::vec3> positions{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
    const std::vector<unsigned> indices{0,1,2,0,2,3};
    const std::vector<glm::vec3> flatNormals(4,{0,0,1});
    mesh->setData(positions,indices,flatNormals,{{0,0},{1,0},{1,1},{0,1}});
    auto object=std::make_shared<vr::Transform>();
    object->addChild(mesh);
    scene.getRoot()->addChild(object);
    auto light=std::make_shared<vr::Light>();
    light->setPosition({2,1,4,1});
    scene.getRoot()->addChild(light);
    auto camera=scene.getCamera();
    camera->setPerspective(50,static_cast<float>(width)/height,.1f,40);
    const auto render=[&](bool parallax,bool useDeferred=true){
        state->setUniform("uParallaxMapping",parallax);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,width,height);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        renderer.render(*scene.getRoot(),*camera,nullptr,nullptr,useDeferred?&deferred:nullptr);
        std::vector<unsigned char> image(width*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        check(glGetError()==GL_NO_ERROR,"Parallax rendering respects the OpenGL sampler budget/state");
        return image;
    };
    const auto center=(height/2*width+width/2)*4;
    const auto sample=[&](unsigned attachment){
        const auto values=deferred.getGBuffer().read(attachment);
        return glm::vec4(values[center],values[center+1],values[center+2],values[center+3]);
    };
    for (const float side : {-1.0f,1.0f}){
        camera->setLookAt({side*1.2f,0,4},{0,0,0});
        const auto reference=render(false);
        const auto base=sample(vr::GBuffer::Albedo);
        const auto baseNormal=sample(vr::GBuffer::Normal);
        const auto world=deferred.getGBuffer().read(vr::GBuffer::Position);
        const auto plane=deferred.getGBuffer().read(vr::GBuffer::Plane);
        const auto parallax=render(true);
        const auto shifted=sample(vr::GBuffer::Albedo);
        const float expected=.4f*side*1.2f/4/2*(1-64.0f/255)*256/255;
        std::cout << "UV displacement side " << side << ": " << base.x-shifted.x
                  << " (expected " << expected << ")\n";
        check(std::abs((base.x-shifted.x)-expected)<1.1f/255,
              "Constant-height relief produces the analytic view-dependent UV displacement");
        check(std::abs(base.y-shifted.y)<.001f && std::abs(baseNormal.x-sample(vr::GBuffer::Normal).x)>.005f,
              "Normal-map lookup follows the same displaced UV as the diffuse map");
        check(reference!=parallax,"Parallax changes the rendered surface, not just material metadata");
        check(world==deferred.getGBuffer().read(vr::GBuffer::Position) &&
                  plane==deferred.getGBuffer().read(vr::GBuffer::Plane) && mesh->getTriangleCount()==2,
              "Parallax leaves world geometry, receiver planes and silhouettes unchanged");
        const auto forward=render(true,false);
        for (unsigned c=0;c<3;++c) check(std::abs(int(parallax[center+c])-int(forward[center+c]))<=2,
              "Forward and deferred evaluate the same displaced normal/diffuse surface");
        state->setUniform("uBumpMapping",false);
        const auto withoutBump=render(true);
        check(glm::distance(glm::vec3(sample(vr::GBuffer::Normal)),glm::vec3(0,0,1))<.002f &&
                  sample(vr::GBuffer::Albedo)==shifted,
              "Bump OFF flattens the normal while preserving independently enabled parallax UVs");
        const auto forwardWithoutBump=render(true,false);
        for(unsigned c=0;c<3;++c)check(std::abs(int(withoutBump[center+c])-int(forwardWithoutBump[center+c]))<=2,
              "Forward and deferred agree on parallax with normal perturbations disabled");
        state->setUniform("uBumpMapping",true);
        state->setUniform("uParallaxScale",0.0f);
        check(render(true)==reference,"Zero depth restores normal mapping exactly");
        state->setUniform("uParallaxScale",.4f);
    }
    camera->setLookAt({1.2f,0,4},{0,0,0});
    render(false); const auto base=sample(vr::GBuffer::Albedo);
    render(true); const float original=base.x-sample(vr::GBuffer::Albedo).x;
    object->setMatrix(glm::scale(glm::mat4(1),{2,.7f,3}));
    render(false); const auto scaled=sample(vr::GBuffer::Albedo);
    render(true);
    check(std::abs(scaled.x-sample(vr::GBuffer::Albedo).x-original*.5f)<1.6f/255,
          "Physical parallax depth accounts for nonuniform model scale");
    object->setMatrix(glm::mat4(1));
    mesh->setData(positions,indices,flatNormals,{{1,0},{0,0},{0,1},{1,1}});
    render(false); const auto mirrored=sample(vr::GBuffer::Albedo);
    render(true);
    check(sample(vr::GBuffer::Albedo).x>mirrored.x+.03f,"Mirrored UV charts reverse the parallax offset");
    mesh->setData(positions,indices,flatNormals,{{0,0},{1,0},{1,1},{0,1}});
    deferred.setAntialiasing(vr::AntialiasingMode::Msaa);
    render(false); const auto msaa=sample(vr::GBuffer::Albedo);
    render(true);
    check(msaa.x-sample(vr::GBuffer::Albedo).x>.03f,"MSAA geometry samples retain parallax displacement");
    deferred.setAntialiasing(vr::AntialiasingMode::Off);
    renderer.setShadingMode(vr::ShadingMode::UnmappedPhong);
    check(render(true)==render(false),"Plain Phong disables parallax together with other surface detail");
    renderer.setShadingMode(vr::ShadingMode::Phong);
    // Constant height alone cannot distinguish relief from a uniform UV shift.
    // Check spatially varying profiles against an independent dense ray search.
    const auto variableField=[&](const std::shared_ptr<vr::Texture>& heightMap,float depth,const char* label){
        state->setHeightMap(heightMap);state->setUniform("uParallaxScale",depth);
        const auto packed=heightMap->withNormal(state->getNormalMap());
        packed->apply(4);
        // This is a magnified surface comparison: sample the base profile exactly.
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        // Measure UVs without the production albedo buffer's 8-bit rounding.
        deferred.getGBuffer().get(vr::GBuffer::Albedo)->apply(0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
        state->getTexture(0)->apply(0);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        camera->setLookAt({2,0,2},{0,0,0});
        render(false);
        const auto baseColor=deferred.getGBuffer().read(vr::GBuffer::Albedo);
        const auto positions=deferred.getGBuffer().read(vr::GBuffer::Position);
        render(true);
        const auto shiftedColor=deferred.getGBuffer().read(vr::GBuffer::Albedo);
        float minimum=1,maximum=0,maxError=0,maxRawError=0;unsigned samples=0;
        for(int y=height/4;y<3*height/4;y+=7)for(int x=width/4;x<3*width/4;x+=7){
            const auto i=(y*width+x)*4;
            if(positions[i+3]<.5f)continue;
            const glm::vec2 uv=(glm::vec2(positions[i],positions[i+1])+1.0f)*.5f;
            if(uv.x<.15f || uv.x>.85f || uv.y<.15f || uv.y>.85f)continue;
            const glm::vec2 ray=depth*.5f*glm::vec2(2-positions[i],-positions[i+1])/2.0f;
            const auto denseHit=[&](float rounding){
                float hit=0;
                // Thousands of tiny steps, rather than the shader's coarse bracket.
                for(int j=0;j<=8192;++j){
                    hit=j/8192.0f;
                    if(hit>=1-glm::clamp(packed->sampleAlpha(uv-ray*hit)+rounding,0.0f,1.0f))break;
                }
                return uv-ray*hit;
            };
            const glm::vec2 expected=denseHit(0);
            const glm::vec2 actual=(glm::vec2(shiftedColor[i],shiftedColor[i+1])*255.0f+.5f)/256.0f;
            maxRawError=std::max(maxRawError,glm::length(actual-expected));
            // RGBA8 hardware filtering has finite fractional precision. At a
            // near-tangent ridge, one height LSB can select a different crossing.
            // Bound that uncertainty explicitly, without relaxing UV accuracy.
            const auto near=denseHit(1.0f/255),far=denseHit(-1.0f/255),span=far-near;
            const float along=glm::clamp(glm::dot(actual-near,span)/std::max(glm::dot(span,span),1e-14f),0.0f,1.0f);
            maxError=std::max(maxError,glm::length(actual-(near+along*span)));
            const float offset=glm::length(uv-actual);
            minimum=std::min(minimum,offset);maximum=std::max(maximum,offset);++samples;
        }
        std::cout<<label<<": "<<samples<<" samples, offset range "<<minimum<<".."<<maximum
                 <<" UV, max raw error "<<maxRawError<<", error outside height-rounding bounds "<<maxError<<" UV\n";
        check(samples>30 && maxError<.0005f,"Varying-height intersections agree with dense ray search within one texel and one height LSB");
        check(maximum-minimum>.002f,"Different heights produce different shifts, not a uniform texture translation");
    };
    std::vector<unsigned char> ramp(64*64*4);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
        const auto i=(y*64+x)*4;const auto value=static_cast<unsigned char>(32+3*x);
        ramp[i]=ramp[i+1]=ramp[i+2]=value;ramp[i+3]=255;
    }
    variableField(std::make_shared<vr::Texture>(64,64,std::move(ramp),false),.14f,"Sloped height profile");
    auto brickHeight=vr::Texture::fromFile(path/"experiments/pantheon/textures/red_brick-parallax-height.png");
    variableField(brickHeight,.035f,"Actual brick, 35 mm");
    variableField(brickHeight,.14f,"Actual brick, 140 mm demonstration");
    mesh->setData(positions,indices,flatNormals);
    check(render(true)==render(false),"Missing or degenerate UV coordinates safely disable parallax");
    mesh->setData(positions,indices,flatNormals,{{0,0},{1,0},{1,1},{0,1}});
    camera->setLookAt({4,0,.02f},{0,0,0});
    render(true);
    const auto grazing=deferred.getGBuffer().read(vr::GBuffer::Albedo);
    check(std::all_of(grazing.begin(),grazing.end(),[](float v){return std::isfinite(v);}),
          "Grazing views remain finite instead of generating unbounded offsets");
}
} // namespace

int main(int argc,char** argv){
    if (argc!=2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(width,height,"Parallax mapping tests",nullptr,nullptr);
    if (!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);
    int result=0;
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) result=1;
    else try{packing();importing(argv[1]);rendering(argv[1]);std::cout<<checks<<" parallax checks passed\n";}
    catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
