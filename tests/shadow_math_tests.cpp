#include <vr/ShadowRenderer.h>
#include <vr/ShadowCameras.h>
#include <vr/SceneGraph.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace{
unsigned checks = 0;

void check(bool value, const char* message){
    ++checks;
    if (!value){
        throw std::runtime_error(message);
    }
}

template <class F> void rejects(F f, const char* message){
    bool caught = false;
    try{
        f();
    } catch (const std::exception&){
        caught = true;
    }
    check(caught, message);
}
} // namespace

int main(){
    try{
        vr::Camera camera;
        const vr::BoundingBox bounds({-28, -0.2f, -24},{28, 6, 10});
        for (auto direction :
             {glm::vec3(-0.4f, 1, 0.6f), glm::vec3(0, 1, 0), glm::vec3(1, 0.01f, 0)}){
            vr::fitDirectionalCamera(camera, bounds, direction);
            const auto vp = camera.getProjection() * camera.getView();
            for (auto corner : bounds.corners()){
                const auto p = vp * glm::vec4(corner, 1);
                const auto ndc = glm::vec3(p) / p.w;
                check(glm::all(glm::lessThanEqual(glm::abs(ndc), glm::vec3(1.0001f))),
                      "Light orthographic camera covers the complete scene, independently of "
                      "observer");
            }
        }
        rejects([&]{ camera.setOrthographic(1, -1, -1, 1, 0.1f, 10); },
                "Invalid orthographic range rejected");
        rejects([&]{ vr::fitDirectionalCamera(camera, bounds,{0, 0, 0}); },
                "Zero light direction rejected");
        std::array<std::shared_ptr<vr::Camera>, 6> cameras;
        for (auto& face : cameras){
            face = std::make_shared<vr::Camera>();
        }
        const glm::vec3 position(4, 5, 6);
        vr::configurePointCameras(cameras, position, 60);
        const std::array<glm::vec3, 6> axes ={glm::vec3(1, 0, 0),{-1, 0, 0},{0, 1, 0},
                                               {0, -1, 0},{0, 0, 1},{0, 0, -1}};
        for (unsigned face = 0; face < 6; ++face){
            check(glm::length(cameras[face]->getPosition() - position) < 1e-5f,
                  "Each cubemap camera is at the light");
            const auto p = cameras[face]->getProjection() * cameras[face]->getView() *
                           glm::vec4(position + axes[face] * 3.0f, 1);
            check(std::abs(p.x / p.w) < 1e-5f && std::abs(p.y / p.w) < 1e-5f && p.z / p.w < 1,
                  "Each axis projects to its own face center");
        }
        auto flat = vr::Texture::depth(128, 64), cube = vr::Texture::depth(64, 64, true);
        check(flat->isDepth() && flat->getTarget() == GL_TEXTURE_2D, "Depth 2D resource");
        check(cube->isDepth() && cube->getTarget() == GL_TEXTURE_CUBE_MAP, "Depth cube resource");
        rejects([]{ vr::Texture::depth(64, 32, true); }, "Cubemap must be square");
        rejects([]{ vr::Texture::depth(0, 64); }, "Depth dimensions must be positive");
        vr::State parent, child;
        parent.setCastShadows(true);
        parent.setReceiveShadows(true);
        child.setCastShadows(false);
        child.setReceiveShadows(false);
        const auto resolved = parent.merged(child);
        check(!resolved.getCastShadows() && !resolved.getReceiveShadows(),
              "Local shadow flags override inherited state");
        check(parent.getCastShadows() && parent.getReceiveShadows(),
              "Shadow state merging leaves parent unchanged");
        parent.setShadowMap(0, flat);
        parent.setShadowMap(1, cube);
        rejects([&]{ parent.setShadowMap(0, cube); }, "Shadow sampler target mismatch rejected");
        rejects([&]{ parent.setShadowMap(vr::State::MaxShadowMaps, flat); },
                "Shadow sampler bounds checked");
        vr::Light spot;
        spot.setSpot({0, 0, -4}, 14, 20);
        spot.setRange(35);
        check(spot.getSpot() &&
                  glm::length(spot.getSpotDirection() - glm::vec3(0, 0, -1)) < 1e-5f &&
                  spot.getRange() == 35,
              "Spotlight normalizes its beam direction and retains its range");
        rejects([&]{ spot.setSpot({0, 0, 0}, 14, 20); }, "Zero beam direction rejected");
        rejects([&]{ spot.setSpot({0, 0, -1}, 25, 20); }, "Inverted beam cone rejected");
        rejects([&]{ spot.setRange(-1); }, "Negative light range rejected");
        std::cout << checks << " shadow math/resource checks passed\n";
        return 0;
    } catch (const std::exception& e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
