#include <functional>
#include <map>
#include <vr/Geometry.h>
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
    if (auto source = std::dynamic_pointer_cast<SkeletalAnimation>(asset->getUpdateCallback())){
        auto animation = source->instantiate();
        instance->m_animation = animation;
        instance->setUpdateCallback(animation);
        std::map<const SkinningPose*, std::shared_ptr<SkinningPose>> palettes;
        for (std::size_t i = 0; i < source->getBindings().size(); ++i){
            palettes[source->getBindings()[i].pose.get()] = animation->getBindings()[i].pose;
        }
        std::map<const Node*, std::shared_ptr<Node>> copies;
        const std::function<std::shared_ptr<Node>(const std::shared_ptr<Node>&)> clone =
            [&](const auto& node) -> std::shared_ptr<Node>{
            if (auto found = copies.find(node.get()); found != copies.end()){
                return found->second;
            }
            std::shared_ptr<Node> copy;
            if (auto geometry = std::dynamic_pointer_cast<Geometry>(node)){
                copy = geometry->getSkin()
                           ? geometry->instantiateSkin(palettes.at(geometry->getSkin().get()))
                           : node;
            } else if (auto group = std::dynamic_pointer_cast<Group>(node)){
                std::shared_ptr<Group> branch;
                if (auto transform = std::dynamic_pointer_cast<Transform>(node)){
                    auto transformed = std::make_shared<Transform>(node->getName());
                    transformed->setMatrix(transform->getMatrix());
                    branch = transformed;
                } else{
                    branch = std::make_shared<Group>(node->getName());
                }
                copy = branch;
                copies[node.get()] = copy;
                for (const auto& child : group->getChildren()){
                    branch->addChild(clone(child));
                }
            } else{
                throw std::runtime_error("Unsupported animated model node");
            }
            if (copy != node){
                copy->setState(node->getState());
                copy->setVisible(node->getVisible());
                copy->setCullingEnabled(node->getCullingEnabled());
                copy->setRoomId(node->getRoomId());
                auto callback = node->getUpdateCallback();
                if (node == asset){
                    callback = std::dynamic_pointer_cast<Transform>(copy)
                                   ? std::make_shared<JointAnimationCallback>(animation, 0)
                                   : nullptr;
                } else if (auto joint =
                               std::dynamic_pointer_cast<JointAnimationCallback>(callback)){
                    callback =
                        std::make_shared<JointAnimationCallback>(animation, joint->getJoint());
                }
                copy->setUpdateCallback(std::move(callback));
            }
            copies[node.get()] = copy;
            return copy;
        };
        placed = clone(asset);
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
