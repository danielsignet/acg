#pragma once
#include "Geometry.h"
#include "GraphVisitor.h"
#include <algorithm>
#include <cmath>

namespace vr{
struct TreeShape{float height=0;glm::vec4 warp{0};};
inline TreeShape treeShape(const State& state){
    TreeShape shape;const auto& u=state.getUniforms();
    if(auto i=u.find("uTreeHeight");i!=u.end())shape.height=std::get<float>(i->second);
    if(auto i=u.find("uTreeWarp");i!=u.end())shape.warp=std::get<glm::vec4>(i->second);
    return shape;
}
inline glm::vec3 treePoint(glm::vec3 p,const TreeShape& shape){
    if(shape.height<=0)return p;
    const float h=std::max(p.y,0.f)/shape.height,a=shape.warp.z*h;
    const float c=std::cos(a),s=std::sin(a),stretch=std::max(.2f,1+shape.warp.w*h);
    const glm::vec2 q(c*p.x+s*p.z,-s*p.x+c*p.z);
    const auto bent=q*stretch+glm::vec2(shape.warp)*h*h;
    return {bent.x,p.y,bent.y};
}
// A local Lipschitz enclosure stays tight for small BVH nodes without rebuilding
// or duplicating the shared mesh for each tree's different bend/twist/taper.
inline BoundingBox treeBound(const BoundingBox& box,const TreeShape& shape){
    if(shape.height<=0 || !box.valid())return box;
    const auto e=box.getExtent();const float h=std::max(0.f,box.getMaximum().y)/shape.height;
    const float radius=glm::length(glm::vec2(std::max(std::abs(box.getMinimum().x),std::abs(box.getMaximum().x)),
        std::max(std::abs(box.getMinimum().z),std::abs(box.getMaximum().z))));
    const float scale=1+std::abs(shape.warp.w)*h;
    const auto slope=(glm::vec2((std::abs(shape.warp.w)+scale*std::abs(shape.warp.z))*radius)+
                      2.f*glm::abs(glm::vec2(shape.warp))*h)/shape.height;
    const auto extent=glm::vec2(scale*(e.x+e.z))+slope*e.y;
    const auto center=treePoint(box.getCenter(),shape);const glm::vec3 padding(extent.x,e.y,extent.y);
    return BoundingBox(center-padding,center+padding);
}
inline void prepareTreeBounds(Node& node,const State& inherited=State{}){
    GraphVisitor visitor(inherited);
    visitor.setGeometryHandler([](Geometry& mesh,const State& state){
        const auto shape=treeShape(state);if(shape.height<=0)return;
        BoundingBox raw;for(const auto& vertex:mesh.getVertices())raw.expandBy(vertex.position);
        if(!raw.valid())return;const auto deformed=treeBound(raw,shape);
        const auto padding=glm::max(glm::abs(deformed.getMinimum()-raw.getMinimum()),glm::abs(deformed.getMaximum()-raw.getMaximum()));
        mesh.setMotionPadding(glm::max(mesh.getMotionPadding(),padding));
    });
    node.accept(visitor);
}
}
