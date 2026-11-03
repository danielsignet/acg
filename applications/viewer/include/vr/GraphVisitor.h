#pragma once
#include "Geometry.h"
#include "Group.h"
#include <functional>

namespace vr{
// Unrestricted traversal for authoring/preparation; every DAG path keeps its inherited state.
class GraphVisitor : public NodeVisitor{
public:
    explicit GraphVisitor(State initial={}):m_state(std::move(initial)){}
    void setNodeHandler(std::function<void(Node&)> handler){onNode=std::move(handler);}
    void setGeometryHandler(std::function<void(Geometry&,const State&)> handler){onGeometry=std::move(handler);}
    void setTransformHandler(std::function<void(Transform&)> handler){onTransform=std::move(handler);}
    void apply(Group& node) override{
        auto parent=m_state;
        enter(node);traverse(node);m_state=std::move(parent);
    }
    void apply(Transform& node) override;
    void apply(Geometry& node) override{
        auto parent=m_state;enter(node);
        if(onGeometry)onGeometry(node,m_state);
        m_state=std::move(parent);
    }
    void apply(Light& node) override;
    void apply(Camera& node) override;
private:
    void enter(Node& node){if(node.getState())m_state=m_state.merged(*node.getState());if(onNode)onNode(node);}
    State m_state;
    std::function<void(Node&)> onNode;
    std::function<void(Geometry&,const State&)> onGeometry;
    std::function<void(Transform&)> onTransform;
};
}
