#pragma once
#include "Billboard.h"
#include "Geometry.h"
#include "LOD.h"
#include <functional>
#include <map>
#include <stdexcept>

namespace vr{
// Typed cloning for imported/subdivided DAGs; shared source nodes stay shared.
class GraphCloneVisitor final:public NodeVisitor{
public:
    void setGeometryHandler(std::function<std::shared_ptr<Node>(Geometry&)> handler){geometry=std::move(handler);}
    void setCallbackHandler(std::function<std::shared_ptr<UpdateCallback>(Node&,bool)> handler){callback=std::move(handler);}
    std::shared_ptr<Node> clone(const std::shared_ptr<Node>& node){
        if(auto found=m_copies.find(node.get());found!=m_copies.end())return found->second;
        node->accept(*this);
        return m_copies.at(node.get());
    }
    void apply(Geometry& node) override{
        finish(node,geometry?geometry(node):node.shared_from_this(),false);
    }
    void apply(Group& node) override{branch(node,std::make_shared<Group>(node.getName()),false);}
    void apply(Transform& node) override{
        auto copy=std::make_shared<Transform>(node.getName());copy->setMatrix(node.getMatrix());
        branch(node,copy,true);
    }
    void apply(Billboard& node) override{
        auto copy=std::make_shared<Billboard>(node.getName());copy->setMatrix(node.getMatrix());
        branch(node,copy,true);
    }
    void apply(LOD& node) override{
        auto copy=std::make_shared<LOD>(node.getName());m_copies[&node]=copy;
        copy->setCenter(node.getCenter());copy->setFadeWidth(node.getFadeWidth());
        for(std::size_t i=0;i<node.getChildren().size();++i)
            copy->addLevel(node.getRanges()[i],clone(node.getChildren()[i]));
        finish(node,copy,false);
    }
    void apply(Light&) override{throw std::runtime_error("Cannot clone a light as model geometry");}
    void apply(Camera&) override{throw std::runtime_error("Cannot clone a camera as model geometry");}
private:
    void branch(Group& node,const std::shared_ptr<Group>& copy,bool transformed){
        m_copies[&node]=copy;
        for(const auto& child:node.getChildren())copy->addChild(clone(child));
        finish(node,copy,transformed);
    }
    void finish(Node& node,const std::shared_ptr<Node>& copy,bool transformed){
        m_copies[&node]=copy;
        if(copy.get()==&node)return;
        copy->setState(node.getState());copy->setVisible(node.getVisible());
        copy->setCullingEnabled(node.getCullingEnabled());copy->setRoomId(node.getRoomId());
        copy->setUpdateEnabled(node.getUpdateEnabled());
        copy->setUpdateCallback(callback?callback(node,transformed):node.getUpdateCallback());
    }
    std::map<const Node*,std::shared_ptr<Node>> m_copies;
    std::function<std::shared_ptr<Node>(Geometry&)> geometry;
    std::function<std::shared_ptr<UpdateCallback>(Node&,bool)> callback;
};
}
