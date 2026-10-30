#pragma once
#include "Group.h"
#include <limits>

namespace vr{
class LOD : public Group{
public:
    using Group::Group;

    // Dispatches this concrete node type to the visitor.
    void accept(NodeVisitor& visitor) override{
        visitor.apply(*this);
    }

    // Adds one detail level with a strictly increasing distance boundary.
    void addLevel(float maximumDistance, const std::shared_ptr<Node>& node);

    // Selects the nearest matching range and falls back to the last detail level.
    std::size_t selectLevel(float distance) const;

    struct Selection{
        std::size_t nearLevel, farLevel;
        float blend;
    };

    // Selects at most two neighboring levels with a continuous distance-based blend.
    Selection select(float distance) const;

    // Sets the world-space transition width; zero retains discrete detail selection.
    void setFadeWidth(float width);
    float getFadeWidth() const{ return m_fadeWidth; }

    // Returns the exclusive upper distance boundaries of the detail levels.
    const std::vector<float>& getRanges() const{
        return m_ranges;
    }

    // Sets the local point used for camera-distance selection.
    void setCenter(const glm::vec3& center){
        m_center = center;
    }

    // Returns the local point used for camera-distance selection.
    const glm::vec3& getCenter() const{
        return m_center;
    }

private:
    using Group::addChild;
    std::vector<float> m_ranges;
    glm::vec3 m_center{0};
    float m_fadeWidth = 0;
};
} // namespace vr
