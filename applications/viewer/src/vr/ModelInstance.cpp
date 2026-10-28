#include <vr/ModelInstance.h>
#include <vr/Subdivision.h>
#include <vr/SkeletalAnimation.h>

namespace vr{
// Returns this asset's source-animation controller when one was imported.
std::shared_ptr<SkeletalAnimation> ModelInstance::getAnimation() const{
    return std::dynamic_pointer_cast<SkeletalAnimation>(
        m_original->getChildren().front()->getUpdateCallback());
}

// Wraps one placement of a shared model asset with its own variant visibility.
std::shared_ptr<ModelInstance> ModelInstance::create(const std::string& name,
                                                     const std::shared_ptr<Node>& asset,
                                                     bool selectable){
    auto instance = std::make_shared<ModelInstance>(name);
    instance->m_selectable = selectable;
    instance->m_original = std::make_shared<Group>("original");
    instance->m_original->addChild(asset);
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
}
} // namespace vr
