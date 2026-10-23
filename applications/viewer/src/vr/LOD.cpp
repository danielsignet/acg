#include <stdexcept>
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
} // namespace vr
