#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <vr/LOD.h>

namespace vr{
// Adds one detail level with a strictly increasing distance boundary.
void LOD::addLevel(float maximum, const std::shared_ptr<Node>& node){
    if (!(maximum > 0) || (!m_ranges.empty() && maximum <= m_ranges.back())){
        throw std::invalid_argument("LOD ranges must increase");
    }
    Group::addChild(node);
    m_ranges.push_back(maximum);
}

// Selects the nearest matching range and falls back to the last detail level.
std::size_t LOD::selectLevel(float distance) const{
    if (m_ranges.empty()){
        throw std::logic_error("Empty LOD");
    }
    for (std::size_t i = 0; i < m_ranges.size(); ++i){
        if (distance < m_ranges[i]){
            return i;
        }
    }
    return m_ranges.size() - 1;
}

// Sets the world-space transition width; zero retains discrete detail selection.
void LOD::setFadeWidth(float width){
    if (!std::isfinite(width) || width < 0){
        throw std::invalid_argument("LOD fade width must be finite and nonnegative");
    }
    m_fadeWidth = width;
}

// Keeps transition bands disjoint even when configured wider than the detail ranges.
LOD::Selection LOD::select(float distance) const{
    const auto level = selectLevel(distance);
    for (std::size_t i = 0; m_fadeWidth > 0 && i + 1 < m_ranges.size(); ++i){
        const float boundary = m_ranges[i];
        const float previous = i == 0 ? 0 : m_ranges[i - 1];
        const float half = std::min({m_fadeWidth * 0.5f, (boundary - previous) * 0.5f,
                                     (m_ranges[i + 1] - boundary) * 0.5f});
        if (half > 0 && distance > boundary - half && distance < boundary + half){
            const float t = (distance - boundary + half) / (2 * half);
            return{i, i + 1, t * t * (3 - 2 * t)};
        }
    }
    return{level, level, 0};
}
} // namespace vr
