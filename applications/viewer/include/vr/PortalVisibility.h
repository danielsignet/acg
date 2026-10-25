#pragma once
#include "BoundingBox.h"
#include <map>
#include <string>
#include <vector>

namespace vr{
struct ClipVolume{
    std::vector<glm::vec4> planes;

    // Retains bounds that intersect every inward half-space, including unknown bounds.
    bool intersects(const BoundingBox& box) const;

    // Clips an aperture conservatively against the volume's inward planes.
    std::vector<glm::vec3> clip(std::vector<glm::vec3> polygon) const;
};

struct PortalCell{
    std::string id;
    BoundingBox bounds;
};

struct Doorway{
    std::string from, to;
    BoundingBox opening;
};

struct Portal{
    std::size_t from, to;
    std::array<glm::vec3, 4> corners;
    glm::vec4 forwardPlane;
};

struct PortalResult{
    bool active = false;
    std::string origin = "no visibility layout";
    unsigned portalsTested = 0;
    std::map<std::string, std::vector<ClipVolume>> volumes;

    // Reports whether any doorway sequence reaches the cell, or fallback retains all cells.
    bool visible(const std::string& id) const{
        return !active || volumes.count(id) != 0;
    }
};

// Explicit openings connect otherwise solid cells and an optional exterior region.
class PortalGraph{
public:
    // Validates cells and rectangular openings before replacing the visibility graph.
    void build(std::vector<PortalCell> cells, const std::vector<Doorway>& doors,
               std::string exterior ={});

    // Collects visibility volumes through door sequences from a live or frozen eye.
    PortalResult collect(const glm::vec3& eye, const glm::mat4* viewProjection = nullptr) const;

    // Returns the finite room cells, excluding the optional exterior region.
    const std::vector<PortalCell>& getCells() const{
        return m_cells;
    }

    // Returns both directed portals for each physical opening.
    const std::vector<Portal>& getPortals() const{
        return m_portals;
    }

private:
    std::vector<PortalCell> m_cells;
    std::vector<Portal> m_portals;
    std::string m_exterior;
};
} // namespace vr
