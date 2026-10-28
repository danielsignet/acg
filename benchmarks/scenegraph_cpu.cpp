#include <vr/Reader.h>
#include <vr/ModelInstance.h>
#include <vr/SkeletalAnimation.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>

using Clock = std::chrono::steady_clock;

double elapsed(Clock::time_point start){
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::vector<vr::Geometry*> geometries(vr::Node& root){
    std::vector<vr::Geometry*> result;
    std::set<vr::Node*> seen;
    std::function<void(vr::Node&)> visit = [&](vr::Node& node){
        if (!seen.insert(&node).second){
            return;
        }
        if (auto* geometry = dynamic_cast<vr::Geometry*>(&node)){
            result.push_back(geometry);
        }
        if (auto* group = dynamic_cast<vr::Group*>(&node)){
            for (const auto& child : group->getChildren()){
                visit(*child);
            }
        }
    };
    visit(root);
    return result;
}

void measure(std::ostream& output, const char* phase, vr::ModelInstance& instance, bool animate){
    vr::UpdateVisitor updater;
    vr::CullVisitor collector;
    std::vector<double> updates;
    double updateTotal = 0, boundTotal = 0, cullTotal = 0;
    constexpr int warmup = 40, samples = 300;
    for (int frame = 0; frame < warmup + samples; ++frame){
        auto start = Clock::now();
        updater.update(instance,{frame / 60.0, 1.0 / 60, animate});
        const double update = elapsed(start);
        start = Clock::now();
        const auto bound = instance.getBound();
        const double bounds = elapsed(start);
        start = Clock::now();
        collector.collect(instance, glm::mat4(1),{0, 0, 10}, false);
        const double cull = elapsed(start);
        if (frame >= warmup){
            updates.push_back(update);
            updateTotal += update;
            boundTotal += bounds;
            cullTotal += cull;
        }
    }
    std::sort(updates.begin(), updates.end());
    std::size_t allVertices = 0, visibleVertices = 0;
    for (const auto* mesh : geometries(instance)){
        allVertices += mesh->getVertices().size();
    }
    for (const auto& item : collector.getRenderList()){
        visibleVertices += item.geometry->getVertices().size();
    }
    output << phase << ',' << allVertices << ',' << visibleVertices << ','
           << updater.getCallbackCount() << ',' << updateTotal / samples << ','
           << updates[updates.size() / 2] << ',' << updates[updates.size() * 95 / 100] << ','
           << boundTotal / samples << ',' << cullTotal / samples << '\n';
}

int main(int argc, char** argv){
    try{
        if (argc < 3){
            return 1;
        }
        vr::Reader reader(argv[1]);
        auto asset = reader.readModel("models/dragon/Dragon.fbx");
        auto instance = vr::ModelInstance::create("Dragon", asset);
        std::ofstream output(argv[2]);
        output << std::fixed << std::setprecision(6);
        output << "phase,all_vertices,visible_vertices,callbacks,update_mean_ms,update_median_ms,"
                  "update_p95_ms,bounds_mean_ms,cull_without_rejection_mean_ms\n";
        measure(output, "original_animated", *instance, true);
        measure(output, "original_paused", *instance, false);
        instance->setVisible(false);
        measure(output, "original_hidden_animated", *instance, true);
        instance->setVisible(true);
        instance->setSubdivided(true);
        measure(output, "subdivided_animated", *instance, true);
        instance->setSubdivided(false);
        measure(output, "original_after_subdivision_animated", *instance, true);
        measure(output, "original_after_subdivision_paused", *instance, false);
        // Verify that hidden refined vertices really change while the original is displayed.
        auto meshes = geometries(*instance);
        std::vector<std::vector<glm::vec3>> before;
        for (const auto* mesh : meshes){
            std::vector<glm::vec3> positions;
            for (const auto& vertex : mesh->getVertices()){
                positions.push_back(vertex.position);
            }
            before.push_back(std::move(positions));
        }
        vr::UpdateVisitor updater;
        updater.update(*instance,{0, 0.2, true});
        std::size_t moved = 0;
        for (std::size_t mesh = 0; mesh < meshes.size(); ++mesh){
            for (std::size_t vertex = 0; vertex < before[mesh].size(); ++vertex){
                moved += glm::distance(before[mesh][vertex],
                                       meshes[mesh]->getVertices()[vertex].position) > 1e-6f;
            }
        }
        std::cout << "Vertices moved across visible and hidden variants: " << moved << '\n';
        auto second = vr::ModelInstance::create("SecondDragon", asset);
        std::cout << "Independent placements share animation controller: "
                  << (instance->getAnimation() == second->getAnimation()) << '\n';
        output.close();
        std::cout << "Wrote " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
