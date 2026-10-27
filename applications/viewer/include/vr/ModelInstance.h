#pragma once
#include "Group.h"

namespace vr{
class ModelInstance final : public Group{
public:
    using Group::Group;

    // Wraps one placement of a shared model asset with its own variant visibility.
    static std::shared_ptr<ModelInstance>
    create(const std::string& name, const std::shared_ptr<Node>& asset, bool selectable = true);

    // Builds the refined variant on first use and toggles only this model placement.
    void setSubdivided(bool enabled);

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
    bool m_enabled = false;
    bool m_selectable = true;
};
} // namespace vr
