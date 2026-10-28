#pragma once
#include "Group.h"

namespace vr{
class SkeletalAnimation;

class ModelInstance final : public Group{
public:
    using Group::Group;

    // Wraps one placement of a shared model asset with its own variant visibility.
    static std::shared_ptr<ModelInstance>
    create(const std::string& name, const std::shared_ptr<Node>& asset, bool selectable = true);

    // Builds the refined variant on first use and toggles only this model placement.
    void setSubdivided(bool enabled);
    // Returns this asset's source-animation controller when one was imported.
    std::shared_ptr<SkeletalAnimation> getAnimation() const;

    const std::shared_ptr<Group>& getActiveVariant() const{
        return m_enabled ? m_refined : m_original;
    }

    const std::shared_ptr<Group>& getInactiveVariant() const{
        return m_enabled ? m_original : m_refined;
    }

    BoundingBox computeBound() const override;
    bool computeCullingExemption() const override;

    // Reports which cached variant is active for picking and the toolbar.
    bool getSubdivided() const{
        return m_enabled;
    }

    // Reports per-instance picking eligibility independently of its asset format.
    bool getSelectable() const{
        return m_selectable;
    }

private:
    std::shared_ptr<Group> m_original, m_refined;
    std::shared_ptr<SkeletalAnimation> m_animation;
    bool m_enabled = false;
    bool m_selectable = true;
};
} // namespace vr
