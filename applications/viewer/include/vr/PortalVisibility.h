#pragma once
#include "BoundingBox.h"
#include <map>
#include <string>
#include <vector>

namespace vr{
struct ClipVolume{
    std::vector<glm::vec4> planes;
    bool intersects(const BoundingBox& box) const;
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

    bool visible(const std::string& id) const{
        return !active || volumes.count(id) != 0;
    }
};

// Explicit openings connect otherwise solid cells and an optional exterior region.
class PortalGraph{
public:
    void build(std::vector<PortalCell> cells, const std::vector<Doorway>& doors,
               std::string exterior = {});
    PortalResult collect(const glm::vec3& eye, const glm::mat4* viewProjection = nullptr) const;

    const std::vector<PortalCell>& getCells() const{
        return m_cells;
    }

    const std::vector<Portal>& getPortals() const{
        return m_portals;
    }

private:
    std::vector<PortalCell> m_cells;
    std::vector<Portal> m_portals;
    std::string m_exterior;
};
} // namespace vr
