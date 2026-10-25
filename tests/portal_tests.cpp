#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vr/SceneGraph.h>

namespace{
unsigned checks = 0;

void check(bool value, const char* message){
    ++checks;
    if (!value){
        throw std::runtime_error(message);
    }
}

vr::PortalGraph layout(){
    vr::PortalGraph graph;
    graph.build({{"A", {{-7, 0, -7}, {7, 5, 7}}},
                 {"B", {{7, 0, -7}, {21, 5, 7}}},
                 {"C", {{21, 0, -7}, {35, 5, 7}}}},
                {{"A", "B", {{7, 0, -1.5f}, {7, 3.5f, 1.5f}}},
                 {"B", "C", {{21, 0, -1.5f}, {21, 3.5f, 1.5f}}},
                 {"A", "patio", {{-1.5f, 0, 7}, {1.5f, 3.5f, 7}}}},
                "patio");
    return graph;
}

bool visible(const vr::PortalResult& result, const std::string& cell, const glm::vec3& p){
    const auto found = result.volumes.find(cell);
    if (found == result.volumes.end()){
        return false;
    }
    for (const auto& volume : found->second){
        if (volume.intersects({p, p})){
            return true;
        }
    }
    return false;
}

void geometry(){
    auto graph = layout();
    check(graph.getPortals().size() == 6, "Doorways are directed in both directions");
    const auto result = graph.collect({-4, 2, 4});
    check(result.active && result.origin == "A", "Visibility originates inside A");
    check(visible(result, "B", {14, 2, -2.5f}), "View through an open doorway is retained");
    check(!visible(result, "B", {14, 2, 4}), "Solid part of wall blocks room contents");
    check(!result.visible("C"), "Offset door sequence blocks the third room");
    check(graph.collect({-4, 2, 0}).visible("C"), "Aligned doorway sequence reaches C");
    const auto outside = graph.collect({0, 2, 12});
    check(outside.active && outside.origin == "patio", "The patio is a visibility region");
    check(visible(outside, "A", {0, 2, 0}), "Front doorway admits visibility from outside");
    check(!visible(outside, "A", {6, 2, 0}), "Front wall blocks objects beside the doorway");
    check(!graph.collect({0, 2, -12}).visible("A"), "Solid rear wall blocks outside visibility");
    check(visible(graph.collect({0, 2, 0}), "patio", {0, 2, 12}), "An exit reveals patio content");
    check(!graph.collect({-6.99f, 2, 4}).active, "Camera in solid wall falls back conservatively");
    check(!graph.collect({7, 2, 0}).active,
          "Camera on an internal doorway boundary is conservative");
    std::mt19937 random(180);
    std::uniform_real_distribution<float> x(8, 20), y(.1f, 4.9f), z(-6, 6);
    const glm::vec3 eye(-4, 2, 4);
    for (int i = 0; i < 1000; ++i){
        const glm::vec3 point(x(random), y(random), z(random));
        const auto crossing = eye + (point - eye) * ((7 - eye.x) / (point.x - eye.x));
        const bool reference =
            std::abs(crossing.z) <= 1.5f && crossing.y >= 0 && crossing.y <= 3.5f;
        check(visible(result, "B", point) == reference,
              "Independent segment/door intersection oracle");
    }
    const auto vp = glm::perspective(glm::radians(48.0f), 1.6f, .1f, 100.0f) *
                    glm::lookAt(eye, eye + glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0));
    check(!graph.collect(eye, &vp).visible("B"), "Live frustum clips a doorway behind the viewer");
}

void traversal(){
    auto graph = layout();
    const auto portals = graph.collect({-4, 2, 4});
    auto root = std::make_shared<vr::Group>();
    auto mesh = vr::GeometryFactory::createBox();
    for (const auto& position : {glm::vec3(14, 2, -2.5f), glm::vec3(14, 2, 4)}){
        auto instance = std::make_shared<vr::Transform>();
        instance->setRoomId("B");
        instance->setMatrix(glm::scale(glm::translate(glm::mat4(1), position), glm::vec3(.1f)));
        instance->addChild(mesh);
        root->addChild(instance);
    }
    vr::CullVisitor culler;
    culler.collect(*root, glm::mat4(1), {-4, 2, 4}, false, &portals);
    check(culler.getRenderList().size() == 1 && culler.getStats().portalRejected == 1,
          "Portal clipping prunes scene graph instances even when frustum culling is disabled");
    culler.collect(*root, glm::mat4(1), {-4, 2, 4}, false);
    check(culler.getRenderList().size() == 2, "Disabling portals restores all instances");
    vr::State parent, local;
    parent.setBlend(true);
    local.setBlend(false);
    check(parent.merged(local).getBlend() == false && parent.getBlend(),
          "Blend overrides restore independently");
}

void invalid(){
    bool rejected = false;
    try{
        vr::PortalGraph graph;
        graph.build({{"A", {{-1, 0, -1}, {1, 2, 1}}}}, {{"A", "missing", {{0, 0, 1}, {1, 1, 1}}}});
    } catch (const std::invalid_argument&){
        rejected = true;
    }
    check(rejected, "Invalid portal topology is rejected");
}
} // namespace

int main(){
    try{
        geometry();
        traversal();
        invalid();
        std::cout << checks << " portal checks passed\n";
        return 0;
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
