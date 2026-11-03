#pragma once
#include "Geometry.h"
#include "GraphVisitor.h"
#include <algorithm>

namespace vr{
inline float furLength(const State& state){
    const auto it=state.getUniforms().find("uFurLength");
    return it==state.getUniforms().end()?0.0f:std::get<float>(it->second);
}

inline unsigned furLayerCount(const State& state){
    const auto& values=state.getUniforms();
    const auto enabled=values.find("uFurRendering");
    if(furLength(state)<=0 || (enabled!=values.end() && !std::get<bool>(enabled->second))) return 0;
    const auto layers=values.find("uFurLayers");
    return static_cast<unsigned>(std::clamp(layers==values.end()?14:std::get<int>(layers->second),1,64));
}

// Resolve inherited material settings once when authoring/loading a graph. Shared
// meshes retain the largest required envelope, including placements without fur.
inline void prepareFurBounds(Node& node,const State& inherited=State{}){
    GraphVisitor visitor(inherited);
    visitor.setGeometryHandler([](Geometry& mesh,const State& state){
        if(furLength(state)>0){
            const auto bend=state.getUniforms().find("uFurBend");
            const auto padding=glm::vec3(furLength(state))+
                (bend==state.getUniforms().end()?glm::vec3(0):glm::abs(std::get<glm::vec3>(bend->second)));
            mesh.setFurPadding(glm::max(mesh.getFurPadding(),padding));
        }
    });
    node.accept(visitor);
}
}
