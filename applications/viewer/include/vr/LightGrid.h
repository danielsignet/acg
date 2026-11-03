#pragma once
#include "LightCollector.h"
#include <array>

namespace vr{
class Camera;

// Conservative light lists for a fixed grid of screen-space tiles (OpenGL 4.1).
class LightGrid final{
public:
    static constexpr unsigned Columns = 16, Rows = 9, Count = Columns * Rows;

    // Keeps a light wherever its world-space range sphere can intersect a tile frustum.
    void build(const Camera& camera, const std::vector<PositionedLight>& lights,
               bool culling = true);

    // Packs four exact ten-bit masks per uniform vector, without an extra sampler.
    const std::array<glm::vec4, Count / 4>& getMasks() const{ return m_masks; }
    unsigned getMask(unsigned tile) const;
    unsigned getCandidatePairs() const{ return m_candidatePairs; }
    unsigned getPossiblePairs() const{ return m_possiblePairs; }

private:
    std::array<glm::vec4, Count / 4> m_masks{};
    unsigned m_candidatePairs = 0, m_possiblePairs = 0;
};
} // namespace vr
