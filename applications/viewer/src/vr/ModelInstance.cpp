#include <functional>
#include <map>
#include <vr/Geometry.h>
#include <vr/GraphCloneVisitor.h>
#include <vr/ModelInstance.h>
#include <vr/SkeletalAnimation.h>
#include <vr/Subdivision.h>
#include <vr/Transform.h>
#include <vr/UpdateVisitor.h>

namespace vr{
// Returns this asset's source-animation controller when one was imported.
std::shared_ptr<SkeletalAnimation> ModelInstance::getAnimation() const{
    return m_animation;
}

// Wraps one placement of a shared model asset with its own variant visibility.
std::shared_ptr<ModelInstance> ModelInstance::create(const std::string& name,
                                                     const std::shared_ptr<Node>& asset,
                                                     bool selectable){
    auto instance = std::make_shared<ModelInstance>(name);
    instance->m_selectable = selectable;
    instance->m_original = std::make_shared<Group>("original");
    auto placed = asset;
    if (auto source = callbackAs<SkeletalAnimation>(asset->getUpdateCallback())){
        auto animation = source->instantiate();
        instance->m_animation = animation;
        instance->setUpdateCallback(animation);
        std::map<const SkinningPose*, std::shared_ptr<SkinningPose>> palettes;
        for (std::size_t i = 0; i < source->getBindings().size(); ++i){
            palettes[source->getBindings()[i].pose.get()] = animation->getBindings()[i].pose;
        }
        GraphCloneVisitor cloner;
        cloner.setGeometryHandler([&](Geometry& geometry)->std::shared_ptr<Node>{
            return geometry.getSkin()?geometry.instantiateSkin(palettes.at(geometry.getSkin().get()))
                                     :geometry.shared_from_this();
        });
        cloner.setCallbackHandler([&](Node& node,bool transformed)->std::shared_ptr<UpdateCallback>{
            if(&node==asset.get())return transformed?std::make_shared<JointAnimationCallback>(animation,0):nullptr;
            if(auto joint=callbackAs<JointAnimationCallback>(node.getUpdateCallback()))
                return std::make_shared<JointAnimationCallback>(animation,joint->getJoint());
            return node.getUpdateCallback();
        });
        placed=cloner.clone(asset);
    }
    instance->m_original->addChild(placed);
    instance->addChild(instance->m_original);
    return instance;
}

// Builds the refined variant on first use and toggles only this model placement.
void ModelInstance::setSubdivided(bool enabled){
    if (enabled == m_enabled){
        return;
    }
    if (enabled && !m_refined){
        auto variant = std::make_shared<Group>("subdivided");
        variant->addChild(Subdivision::create(m_original->getChildren().front()));
        addChild(variant);
        m_refined = std::move(variant);
    }
    m_original->setVisible(!enabled);
    if (m_refined){
        m_refined->setVisible(enabled);
    }
    m_enabled = enabled;
    dirtyBound();
    // Catch a newly selected variant up to the current pose, including while paused.
    UpdateVisitor updater;
    updater.update(*this,{0, 0, false});
}

BoundingBox ModelInstance::computeBound() const{
    BoundingBox result;
    for (const auto& child : getChildren()){
        if (child != getInactiveVariant()){
            result.expandBy(child->getBound());
        }
    }
    return result;
}

bool ModelInstance::computeCullingExemption() const{
    if (!getCullingEnabled()){
        return true;
    }
    for (const auto& child : getChildren()){
        if (child != getInactiveVariant() && child->hasCullingExemption()){
            return true;
        }
    }
    return false;
}
} // namespace vr
