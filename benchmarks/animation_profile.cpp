#include <vr/FrameProfiler.h>
#include <algorithm>
#include <vr/Reader.h>
#include <vr/ModelInstance.h>
#include <vr/SkeletalAnimation.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace{
using Stage = vr::FrameProfiler::Stage;
using Clock = std::chrono::steady_clock;

double milliseconds(Clock::time_point start){
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void measure(std::ostream& out, const char* phase, vr::ModelInstance& instance, bool animate){
    vr::FrameProfiler profiler(false);
    profiler.setEnabled(true);
    vr::UpdateVisitor updater;
    std::array<double, vr::FrameProfiler::StageCount> times{};
    double bounds = 0, vertices = 0, influences = 0;
    constexpr int warmup = 40, samples = 300;
    for (int frame = 0; frame < warmup + samples; ++frame){
        const auto start = Clock::now();
        profiler.beginFrame(1.0 / 60);
        updater.update(instance,{frame / 60.0, 1.0 / 60, animate, &profiler});
        const auto boundStart = Clock::now();
        instance.getBound();
        const auto boundTime = milliseconds(boundStart);
        profiler.endFrame(milliseconds(start) / 1000);
        if (frame >= warmup){
            const auto& cpu = profiler.getHistory().back().cpu;
            for (std::size_t i = 0; i < times.size(); ++i){
                times[i] += cpu[i] / samples;
            }
            bounds += boundTime / samples;
            vertices += static_cast<double>(updater.getSkinnedVertices()) / samples;
            influences += static_cast<double>(updater.getSkinInfluences()) / samples;
        }
    }
    const auto time = [&](Stage stage){ return times[static_cast<std::size_t>(stage)]; };
    const auto animation = instance.getAnimation();
    std::size_t paletteJoints = 0;
    for (const auto& binding : animation->getBindings()){
        paletteJoints += binding.joints.size();
    }
    const auto animationTime = time(Stage::AnimationTracks) + time(Stage::AnimationHierarchy) +
                               time(Stage::AnimationPalettes) + time(Stage::Skinning);
    out << phase << ',' << animation->getJointCount() << ','
        << animation->getClips()[animation->getClip()].tracks.size() << ',' << paletteJoints << ','
        << vertices << ',' << influences << ',' << (vertices > 0 ? influences / vertices : 0) << ','
        << sizeof(vr::Vertex) << ',' << time(Stage::Update) << ',' << time(Stage::AnimationTracks)
        << ',' << time(Stage::AnimationHierarchy) << ',' << time(Stage::AnimationPalettes) << ','
        << time(Stage::Skinning) << ',' << std::max(0.0, time(Stage::Update) - animationTime) << ','
        << bounds << '\n';
}
} // namespace

int main(int argc, char** argv){
    try{
        if (argc != 3){
            throw std::invalid_argument("animation_profile <asset root> <output.csv>");
        }
        vr::Reader reader(argv[1]);
        auto asset = reader.readModel("models/dragon/Dragon.fbx");
        auto instance = vr::ModelInstance::create("Dragon", asset);
        std::ofstream out(argv[2]);
        if (!out){
            throw std::runtime_error("Could not open benchmark output");
        }
        out << std::fixed << std::setprecision(6);
        out << "phase,skeleton_joints,animation_tracks,palette_joints,skinned_vertices,joint_"
               "influences,"
               "influences_per_vertex,vertex_bytes,update_ms,tracks_ms,hierarchy_ms,palettes_ms,"
               "skinning_ms,scene_remainder_ms,bounds_ms\n";
        measure(out, "original_idle", *instance, true);
        measure(out, "original_paused", *instance, false);
        instance->setVisible(false);
        measure(out, "hidden_idle", *instance, true);
        instance->setVisible(true);
        instance->setSubdivided(true);
        measure(out, "subdivided_idle", *instance, true);
        instance->setSubdivided(false);
        for (std::size_t clip = 0; clip < instance->getAnimation()->getClips().size(); ++clip){
            if (instance->getAnimation()->getClips()[clip].name == "Fly"){
                instance->getAnimation()->setClip(clip);
                measure(out, "original_fly", *instance, true);
            }
        }
        std::cout << "Wrote animation breakdown to " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
