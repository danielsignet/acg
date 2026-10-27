#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vr/SceneGraph.h>
#include <vr/ShadowCollector.h>

namespace{
unsigned checks = 0;

void check(bool value, const char* message){
    ++checks;
    if (!value){
        throw std::runtime_error(message);
    }
}

bool near(glm::vec3 a, glm::vec3 b){
    return glm::length(a - b) < 0.0001f;
}

void bounds(){
    vr::BoundingBox empty;
    check(!empty.valid(), "Empty bound");
    vr::BoundingBox cube({-1, -2, -3},{1, 2, 3});
    check(near(cube.getCenter(),{0, 0, 0}) && near(cube.getExtent(),{1, 2, 3}),
          "Box center and half extent");
    auto matrix = glm::scale(glm::rotate(glm::translate(glm::mat4(1),{5, 2, 1}), 0.7f,{0, 1, 0}),
                             {-2, 1, 0.5f});
    matrix[1][0] += 0.4f;
    const auto transformed = cube.transformed(matrix);
    for (auto point : cube.corners()){
        const auto p = glm::vec3(matrix * glm::vec4(point, 1));
        check(glm::all(glm::greaterThanEqual(p, transformed.getMinimum() - glm::vec3(1e-5f))) &&
                  glm::all(glm::lessThanEqual(p, transformed.getMaximum() + glm::vec3(1e-5f))),
              "Affine box encloses all corners including negative scale and shear");
    }
    auto geometry = vr::GeometryFactory::createBox();
    auto a = std::make_shared<vr::Transform>("a"), b = std::make_shared<vr::Transform>("b");
    auto root = std::make_shared<vr::Group>("root");
    a->addChild(geometry);
    b->addChild(geometry);
    root->addChild(a);
    root->addChild(b);
    b->setMatrix(glm::translate(glm::mat4(1),{10, 0, 0}));
    check(near(root->getBound().getMinimum(),{-0.5f, -0.5f, -0.5f}) &&
              near(root->getBound().getMaximum(),{10.5f, 0.5f, 0.5f}),
          "DAG union includes each placement");
    const auto before = root->getBoundBuildCount();
    root->getBound();
    root->getBound();
    check(root->getBoundBuildCount() == before, "Static bounds are cached");
    a->setMatrix(glm::translate(glm::mat4(1),{-10, 0, 0}));
    check(root->isBoundDirty() && !b->isBoundDirty(), "Transform dirties ancestors only");
    root->getBound();
    geometry->setData({{-2, 0, 0},{2, 0, 0},{0, 2, 0}},{0, 1, 2},
                      {{0, 0, 1},{0, 0, 1},{0, 0, 1}});
    check(a->isBoundDirty() && b->isBoundDirty() && root->isBoundDirty(),
          "Geometry mutation dirties all DAG parents");
    check(near(root->getBound().getMinimum(),{-12, 0, 0}) &&
              near(root->getBound().getMaximum(),{12, 2, 0}),
          "Dirty bounds rebuilt with new geometry");
    check(root->getBoundBuildCount() == before + 2, "Only dirty bound rebuilds counted");
}

void frustums(){
    const vr::Frustum clip(glm::mat4(1));
    check(clip.classify({{-0.5f, -0.5f, -0.5f},{0.5f, 0.5f, 0.5f}}) == vr::Visibility::Inside,
          "Identity clip volume inside");
    check(clip.classify({{0.9f, 0, 0},{1.1f, 0.1f, 0.1f}}) == vr::Visibility::Intersecting,
          "Intersecting box retained");
    check(clip.classify({{1, 0, 0},{2, 0.1f, 0.1f}}) != vr::Visibility::Outside,
          "Touching box retained");
    for (int axis = 0; axis < 3; ++axis){
        for (float sign :{-1.0f, 1.0f}){
            glm::vec3 center(0), size(0.1f);
            center[axis] = sign * 2;
            check(clip.classify({center - size, center + size}) == vr::Visibility::Outside,
                  "All six planes reject");
        }
    }
    const auto vp = glm::perspective(glm::radians(55.0f), 1.6f, 0.1f, 90.0f) *
                    glm::lookAt(glm::vec3(7, 5, 12), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    const vr::Frustum frustum(vp);
    const auto corners = vr::Frustum::corners(vp);
    for (unsigned i = 0; i < 8; ++i){
        const auto p = vp * glm::vec4(corners[i], 1);
        const auto ndc = glm::vec3(p) / p.w;
        check(near(ndc,{i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f}),
              "Inverse frustum corners project to clip corners");
    }
    std::mt19937 random(180);
    std::uniform_real_distribution<float> coordinate(-50, 50), size(0.01f, 8);
    for (int sample = 0; sample < 2000; ++sample){
        const glm::vec3 center(coordinate(random), coordinate(random), coordinate(random)),
            radius(size(random), size(random), size(random));
        const vr::BoundingBox box(center - radius, center + radius);
        std::array<bool, 6> allOutside{true, true, true, true, true, true};
        for (auto corner : box.corners()){
            const auto p = vp * glm::vec4(corner, 1);
            const std::array<float, 6> distances ={p.w + p.x, p.w - p.x, p.w + p.y,
                                                    p.w - p.y, p.w + p.z, p.w - p.z};
            for (unsigned plane = 0; plane < 6; ++plane){
                allOutside[plane] = allOutside[plane] && distances[plane] < 0;
            }
        }
        const bool reference =
            std::any_of(allOutside.begin(), allOutside.end(), [](bool value){ return value; });
        check(frustum.classify(box) != vr::Visibility::Outside || reference,
              "Plane extraction cannot falsely reject a box (independent clip-coordinate oracle)");
    }
}

void traversal(){
    auto root = std::make_shared<vr::Group>("root"),
         outside = std::make_shared<vr::Group>("outside"),
         inside = std::make_shared<vr::Group>("inside");
    root->addChild(outside);
    root->addChild(inside);
    outside->setRoomId("A");
    inside->setRoomId("B");
    auto box = vr::GeometryFactory::createBox();
    auto far = std::make_shared<vr::Transform>("far");
    far->setMatrix(glm::translate(glm::mat4(1),{4, 0, 0}));
    far->addChild(box);
    outside->addChild(far);
    auto nearNode = std::make_shared<vr::Transform>("near");
    nearNode->setMatrix(glm::scale(glm::mat4(1), glm::vec3(0.5f)));
    nearNode->addChild(box);
    inside->addChild(nearNode);
    auto parentState = std::make_shared<vr::State>();
    parentState->setUniform("test", 1.0f);
    root->setState(parentState);
    auto childState = std::make_shared<vr::State>();
    childState->setUniform("test", 2.0f);
    nearNode->setState(childState);
    vr::CullVisitor culler;
    culler.collect(*root, glm::mat4(1),{0, 0, 0}, true);
    check(culler.getRenderList().size() == 1 && culler.getRenderList()[0].room == "B",
          "Outside room pruned, room tag inherited");
    check(culler.getStats().subtreesRejected == 1 && culler.getStats().boundsTested == 3,
          "Hierarchical reject and fully inside fast path");
    const auto& draw = culler.getRenderList()[0];
    check(std::get<float>(draw.state.getUniforms().at("test")) == 2.0f &&
              std::get<float>(parentState->getUniforms().at("test")) == 1.0f,
          "Render list resolves state without modifying ancestors");
    check(near(glm::vec3(draw.model[0]),{0.5f, 0, 0}), "Render list accumulates model once");
    culler.collect(*root, glm::mat4(1),{0, 0, 0}, false);
    check(culler.getRenderList().size() == 2 && culler.getStats().boundsTested == 0,
          "Disabled culling visits both shared geometry placements");
    inside->setVisible(false);
    culler.collect(*root, glm::mat4(1),{0, 0, 0}, false);
    check(culler.getRenderList().size() == 1, "Layer visibility independent of culling toggle");
    auto lod = std::make_shared<vr::LOD>("lod");
    lod->addLevel(2, box);
    lod->addLevel(100, vr::GeometryFactory::createSphere(8, 6));
    culler.collect(*lod, glm::mat4(1),{0, 0, 10}, true);
    check(culler.getStats().lodLevels.at("lod") == 1,
          "LOD uses observer position independently of frozen culling matrix");
    culler.collect(*lod, glm::mat4(1),{0, 0, 0}, true);
    check(culler.getStats().lodLevels.at("lod") == 0, "LOD switches near observer");
}

// Keeps exempt floors traversable through rejected ancestors without exempting siblings.
void exemptions(){
    auto root = std::make_shared<vr::Group>();
    auto room = std::make_shared<vr::Transform>();
    room->setRoomId("hidden room");
    room->setMatrix(glm::translate(glm::mat4(1),{4, 0, 0}));
    root->addChild(room);
    auto floor = std::make_shared<vr::Group>();
    floor->setCullingEnabled(false);
    floor->addChild(vr::GeometryFactory::createBox());
    room->addChild(floor);
    room->addChild(vr::GeometryFactory::createBox());
    vr::CullVisitor culler;
    culler.collect(*root, glm::mat4(1),{}, true);
    check(culler.getRenderList().size() == 1 && !culler.getRenderList().front().cullingEnabled &&
              root->hasCullingExemption(),
          "An exempt descendant survives ancestor frustum rejection while siblings are culled");
    vr::PortalResult portals;
    portals.active = true;
    culler.collect(*root, glm::mat4(1),{}, false, &portals);
    check(culler.getRenderList().size() == 1 && culler.getStats().portalRejected == 1,
          "A hidden room cannot portal-cull its exempt floor or exempt its other contents");
    vr::ShadowCollector shadows(nullptr);
    const auto casters = shadows.collect(*root,{});
    check(casters.size() == 2 && !casters[0].cullingEnabled && casters[1].cullingEnabled,
          "Shadow casters inherit exemptions independently for each subtree");
    floor->setVisible(false);
    culler.collect(*root, glm::mat4(1),{}, true);
    check(culler.getRenderList().empty(), "Explicit visibility still hides an exempt subtree");
    floor->setVisible(true);
    floor->setCullingEnabled(true);
    check(!root->hasCullingExemption(), "Changing an exemption invalidates its cached ancestors");
    culler.collect(*root, glm::mat4(1),{}, true);
    check(culler.getRenderList().empty(), "Re-enabling culling restores ancestor rejection");
    auto otherRoot = std::make_shared<vr::Group>();
    otherRoot->addChild(floor);
    otherRoot->getBound();
    floor->setCullingEnabled(false);
    check(root->hasCullingExemption() && otherRoot->hasCullingExemption(),
          "Exemption changes propagate through every parent of a shared subtree");
}

} // namespace

int main(){
    try{
        bounds();
        frustums();
        traversal();
        exemptions();
        std::cout << checks << " culling checks passed\n";
        return 0;
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
