#include <vr/SkeletalAnimation.h>
#include <vr/Geometry.h>
#include <vr/ModelInstance.h>
#include <vr/Reader.h>
#include <vr/UpdateVisitor.h>
#include <vr/CullVisitor.h>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace{
// Reports an animation or picking regression to CTest.
void check(bool condition, const char* message){
    if (!condition){
        throw std::runtime_error(message);
    }
}

// Checks interpolated skinning, exact pause/resume and deformation-aware BVH refitting.
void skinning(){
    auto geometry = std::make_shared<vr::Geometry>();
    std::vector<vr::SkinWeights> weights(3);
    for (auto& weight : weights){
        weight.weights[0] = 1;
    }
    geometry->setData({{-0.4f, -0.4f, 0},{0.4f, -0.4f, 0},{0, 0.4f, 0}},{0, 1, 2},
                      {{0, 0, 1},{0, 0, 1},{0, 0, 1}},{},{}, weights);
    auto pose = std::make_shared<vr::SkinningPose>();
    geometry->setSkin(pose);
    vr::SkeletalAnimation::Track track;
    track.joint = 0;
    track.positions ={{0,{0, 0, 0}},{1,{2, 0, 0}},{2,{0, 0, 0}}};
    // Mesh and skeleton use separate nodes; the mesh remains fixed in scene coordinates.
    vr::SkeletalAnimation::Joint meshJoint;
    meshJoint.parent = -1;
    vr::SkeletalAnimation translated({{}, meshJoint},{{"Move", 2,{track}}},
                                     {{1,{0},{glm::mat4(1)}, pose}});
    geometry->updateSkin();
    check(geometry->intersectRay({0, 0, 4},{0, 0, -1}, 10).has_value(),
          "The rest pose builds a valid picking hierarchy");
    translated.update(*geometry,{0.5, 0.5, true});
    geometry->updateSkin();
    check(std::abs(geometry->getVertices()[0].position.x - 0.6f) < 0.00001f,
          "Skinning interpolates translation in seconds");
    check(!geometry->intersectRay({0, 0, 4},{0, 0, -1}, 10) &&
              geometry->intersectRay({1, 0, 4},{0, 0, -1}, 10),
          "Picking refits to the animated surface and rejects its previous position");
    const auto frozen = geometry->getVertices()[0].position;
    translated.update(*geometry,{100, 99.5, false});
    geometry->updateSkin();
    check(geometry->getVertices()[0].position == frozen, "Pause preserves the exact pose");
    translated.update(*geometry,{100.1, 0.1, true});
    geometry->updateSkin();
    check(std::abs(geometry->getVertices()[0].position.x - 0.8f) < 0.00001f,
          "Resume advances by the new delta rather than accumulated paused time");
    translated.update(*geometry,{102.1, 2, true});
    geometry->updateSkin();
    check(glm::distance(geometry->getVertices()[0].position, glm::vec3(0.8f, -0.4f, 0)) < 0.00001f,
          "Playback loops in source-clip time");
}

// Requires sustained motion in both wing tips, rather than a one-frame pose change.
void flight(vr::ModelInstance& instance, vr::SkeletalAnimation& animation,
            vr::UpdateVisitor& updater){
    std::size_t clip = 0;
    while (clip < animation.getClips().size() && animation.getClips()[clip].name != "Fly"){
        ++clip;
    }
    check(clip < animation.getClips().size(), "The source includes a flight clip");
    animation.setClip(clip);
    const auto duration = animation.getClips()[clip].duration;
    check(
        std::abs(duration - 98.0 / 24) < 0.001,
        "Fly uses the original four-second controller cycle instead of the pose-library timeline");
    updater.update(instance,{0, duration * 0.125, true});
    vr::CullVisitor collector;
    collector.collect(instance, glm::mat4(1),{0, 0, 10}, false);
    std::vector<glm::vec3> before, rest;
    float reach = 0;
    for (const auto& item : collector.getRenderList()){
        for (const auto& vertex : item.geometry->getRestVertices()){
            reach = std::max(reach, std::abs(vertex.position.x));
            rest.push_back(vertex.position);
        }
        for (const auto& vertex : item.geometry->getVertices()){
            before.push_back(vertex.position);
        }
    }
    updater.update(instance,{0, duration * 0.375, true});
    unsigned left = 0, right = 0, movingLeft = 0, movingRight = 0;
    std::size_t index = 0;
    for (const auto& item : collector.getRenderList()){
        for (const auto& vertex : item.geometry->getVertices()){
            const bool moves = glm::distance(vertex.position, before[index]) > reach * 0.05f;
            if (rest[index].x < -reach * 0.6f){
                ++left;
                movingLeft += moves ? 1 : 0;
            } else if (rest[index].x > reach * 0.6f){
                ++right;
                movingRight += moves ? 1 : 0;
            }
            ++index;
        }
    }
    check(left > 10 && right > 10 && movingLeft > left / 2 && movingRight > right / 2,
          "Fly keeps both outer wings moving after the initial pose transition");
    const auto frozen = instance.getBound().getMinimum();
    updater.update(instance,{100, 99, false});
    check(instance.getBound().getMinimum() == frozen, "Flight honors scene-animation pause");
}

// Checks every bundled clip, animated subdivision and topology preservation.
void dragon(const std::filesystem::path& root){
    vr::Reader reader(root);
    const auto asset = reader.readModel("models/dragon/Dragon.fbx");
    auto instance = vr::ModelInstance::create("Dragon", asset);
    const auto animation = instance->getAnimation();
    const auto bound = asset->getBound();
    check(std::abs(bound.getMinimum().y - 51.7488f) < 0.002f && bound.getExtent().x > 600 &&
              bound.getExtent().y > 300,
          "The source parent binding preserves the dragon's support height and proportions");
    check(animation && animation->getClips().size() == 4, "The dragon contains four source clips");
    std::set<std::string> names;
    vr::UpdateVisitor updater;
    vr::CullVisitor collector;
    for (std::size_t clip = 0; clip < animation->getClips().size(); ++clip){
        names.insert(animation->getClips()[clip].name);
        animation->setClip(clip);
        updater.update(*instance,{0, 0, false});
        collector.collect(*instance, glm::mat4(1),{0, 0, 10}, false);
        std::vector<glm::vec3> before;
        std::size_t triangles = 0;
        for (const auto& item : collector.getRenderList()){
            check(item.state.getTexture(0) && item.state.getNormalMap(),
                  "Animated submeshes preserve both source texture maps");
            triangles += item.geometry->getTriangleCount();
            for (const auto& vertex : item.geometry->getVertices()){
                before.push_back(vertex.position);
            }
        }
        check(triangles == 37986, "Animation keeps every original triangle");
        updater.update(*instance,{1, animation->getClips()[clip].duration * 0.37, true});
        float motion = 0;
        std::size_t index = 0;
        for (const auto& item : collector.getRenderList()){
            for (const auto& vertex : item.geometry->getVertices()){
                motion = std::max(motion, glm::distance(vertex.position, before[index++]));
                check(std::isfinite(vertex.position.x) &&
                          std::abs(glm::length(vertex.normal) - 1) < 0.001f,
                      "Animation preserves finite vertices and normalized normals");
            }
        }
        check(motion > 0.01f, "Every source clip produces skeletal motion");
    }
    check(names == std::set<std::string>{"Idle", "Walk", "Run", "Fly"},
          "Only the intended original motion clips are exposed");
    flight(*instance, *animation, updater);
    animation->setClip(1);
    updater.update(*instance,{0, 0, false});
    instance->setSubdivided(true);
    collector.collect(*instance, glm::mat4(1),{0, 0, 10}, false);
    std::size_t triangles = 0;
    std::vector<glm::vec3> before;
    for (const auto& item : collector.getRenderList()){
        triangles += item.geometry->getTriangleCount();
        for (const auto& vertex : item.geometry->getVertices()){
            before.push_back(vertex.position);
        }
    }
    check(triangles == 37986 * 4, "Subdivision refines the original control topology in memory");
    updater.update(*instance,{1, 0.5, true});
    float motion = 0;
    std::size_t index = 0;
    for (const auto& item : collector.getRenderList()){
        for (const auto& vertex : item.geometry->getVertices()){
            motion = std::max(motion, glm::distance(vertex.position, before[index++]));
        }
    }
    check(motion > 0.01f, "The subdivided variant follows the same skeleton");
    flight(*instance, *animation, updater);
}
} // namespace

// Runs deterministic skinning and original-animation integration checks without OpenGL.
int main(int argc, char** argv){
    try{
        skinning();
        dragon(argc > 1 ? argv[1] : ".");
        std::cout << "Skeletal animation, pause, picking and subdivision passed\n";
        return 0;
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
