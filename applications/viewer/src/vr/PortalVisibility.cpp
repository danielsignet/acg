#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vr/PortalVisibility.h>
#include <vr/Frustum.h>

namespace vr{
namespace{
constexpr float planeTolerance = 0.0001f;
constexpr float wallMargin = 0.13f;
constexpr unsigned maxPortalPaths = 256;

float planeDistance(const glm::vec4& plane, const glm::vec3& point){
    return glm::dot(glm::vec3(plane), point) + plane.w;
}

bool contains(const BoundingBox& box, const glm::vec3& point, float margin = planeTolerance){
    return glm::all(glm::greaterThan(point, box.getMinimum() + glm::vec3(margin))) &&
           glm::all(glm::lessThan(point, box.getMaximum() - glm::vec3(margin)));
}

// Validates finite, disjoint cells and assigns the optional exterior its own index.
std::map<std::string, std::size_t> validateCells(const std::vector<PortalCell>& cells,
                                                 const std::string& exterior){
    std::map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < cells.size(); ++i){
        const auto& cell = cells[i];
        if (cell.id.empty() || !cell.bounds.valid() ||
            glm::any(glm::lessThanEqual(cell.bounds.getExtent(), glm::vec3(0))) ||
            !ids.emplace(cell.id, i).second){
            throw std::invalid_argument("Invalid or duplicate visibility cell");
        }
        for (std::size_t j = 0; j < i; ++j){
            const auto overlap = glm::min(cell.bounds.getMaximum(), cells[j].bounds.getMaximum()) -
                                 glm::max(cell.bounds.getMinimum(), cells[j].bounds.getMinimum());
            if (glm::all(glm::greaterThan(overlap, glm::vec3(planeTolerance)))){
                throw std::invalid_argument("Overlapping visibility cells");
            }
        }
    }
    if (!exterior.empty() && !ids.emplace(exterior, cells.size()).second){
        throw std::invalid_argument("Exterior duplicates a room");
    }
    return ids;
}

// Requires exactly one flat axis so an opening is a rectangle rather than a volume or line.
int openingAxis(const BoundingBox& opening){
    const auto size = opening.getMaximum() - opening.getMinimum();
    int axis = -1;
    for (int component = 0; component < 3; ++component){
        if (size[component] < planeTolerance){
            if (axis != -1){
                throw std::invalid_argument("Degenerate portal");
            }
            axis = component;
        }
    }
    if (axis == -1){
        throw std::invalid_argument("Portal must be planar");
    }
    return axis;
}

// Checks that the opening fits on a shared face of both finite endpoints.
void validateOpening(const BoundingBox& opening, const std::vector<PortalCell>& cells,
                     std::size_t from, std::size_t to, int axis){
    const auto center = opening.getCenter();
    for (auto endpoint :{from, to}){
        if (endpoint == cells.size()){
            continue;
        }
        const auto& box = cells[endpoint].bounds;
        if (std::min(std::abs(center[axis] - box.getMinimum()[axis]),
                     std::abs(center[axis] - box.getMaximum()[axis])) > planeTolerance){
            throw std::invalid_argument("Portal is not on a cell face");
        }
        for (int component = 0; component < 3; ++component){
            if (component != axis &&
                (opening.getMinimum()[component] < box.getMinimum()[component] - planeTolerance ||
                 opening.getMaximum()[component] > box.getMaximum()[component] + planeTolerance)){
                throw std::invalid_argument("Portal extends outside its cell face");
            }
        }
    }
}

// Orders the aperture corners around the two non-flat axes.
std::array<glm::vec3, 4> openingCorners(const BoundingBox& opening, int axis){
    const auto center = opening.getCenter();
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    std::array<glm::vec3, 4> points;
    for (unsigned i = 0; i < 4; ++i){
        auto p = center;
        p[u] = (i == 1 || i == 2) ? opening.getMaximum()[u] : opening.getMinimum()[u];
        p[v] = i >= 2 ? opening.getMaximum()[v] : opening.getMinimum()[v];
        points[i] = p;
    }
    return points;
}

// Builds one directed portal; the reverse direction shares its aperture.
Portal createPortal(const Doorway& door, const std::vector<PortalCell>& cells,
                    const std::map<std::string, std::size_t>& ids){
    if (!ids.count(door.from) || !ids.count(door.to) || door.from == door.to ||
        !door.opening.valid()){
        throw std::invalid_argument("Invalid portal endpoints or opening");
    }
    const auto from = ids.at(door.from), to = ids.at(door.to);
    const int axis = openingAxis(door.opening);
    const auto center = door.opening.getCenter();
    validateOpening(door.opening, cells, from, to, axis);
    const float delta = from < cells.size() ? center[axis] - cells[from].bounds.getCenter()[axis]
                                            : cells[to].bounds.getCenter()[axis] - center[axis];
    glm::vec3 normal(0);
    normal[axis] = delta > 0 ? 1.0f : -1.0f;
    if (to < cells.size() && from < cells.size() &&
        (cells[to].bounds.getCenter()[axis] - center[axis]) * normal[axis] <= 0){
        throw std::invalid_argument("Portal cells must be on opposite sides");
    }
    const auto points = openingCorners(door.opening, axis);
    const glm::vec4 plane(normal, -glm::dot(normal, center));
    return{from, to, points, plane};
}

// Retains geometry when the eye enters a wall or an ambiguous doorway boundary.
std::string boundaryFallback(const std::vector<PortalCell>& cells,
                             const std::vector<Portal>& portals, const glm::vec3& eye,
                             std::size_t origin){
    for (const auto& cell : cells){
        if (!contains(cell.bounds, eye, -wallMargin) || contains(cell.bounds, eye, wallMargin)){
            continue;
        }
        bool inDoor = false;
        for (const auto& portal : portals){
            if (std::abs(planeDistance(portal.forwardPlane, eye)) > wallMargin){
                continue;
            }
            BoundingBox opening;
            for (const auto& point : portal.corners){
                opening.expandBy(point);
            }
            inDoor = inDoor || contains(opening, eye, -wallMargin);
        }
        if (inDoor && origin == cells.size()){
            return "door boundary fallback";
        }
        if (!inDoor){
            return "wall boundary fallback";
        }
    }
    return{};
}

// Extends the inherited volume with the portal plane and eye-to-edge planes.
ClipVolume throughPortal(const ClipVolume& volume, const Portal& portal,
                         const std::vector<glm::vec3>& polygon, const glm::vec3& eye){
    ClipVolume child = volume;
    child.planes.push_back(portal.forwardPlane);
    glm::vec3 center(0);
    for (auto p : polygon){
        center += p;
    }
    center /= static_cast<float>(polygon.size());
    for (std::size_t edge = 0; edge < polygon.size(); ++edge){
        auto normal = glm::cross(polygon[edge] - eye, polygon[(edge + 1) % polygon.size()] - eye);
        const float length = glm::length(normal);
        if (length < 1e-7f){
            continue;
        }
        normal /= length;
        glm::vec4 plane(normal, -glm::dot(normal, eye));
        if (planeDistance(plane, center) < 0){
            plane = -plane;
        }
        child.planes.push_back(plane);
    }
    return child;
}
} // namespace

// Tests bounds conservatively against a clipped visibility volume.
bool ClipVolume::intersects(const BoundingBox& box) const{
    if (!box.valid()){
        return true;
    }
    for (const auto& plane : planes){
        if (planeDistance(plane, box.getCenter()) +
                glm::dot(glm::abs(glm::vec3(plane)), box.getExtent()) <
            -planeTolerance){
            return false;
        }
    }
    return true;
}

// Clips an aperture conservatively against the volume's inward planes.
std::vector<glm::vec3> ClipVolume::clip(std::vector<glm::vec3> polygon) const{
    for (const auto& plane : planes){
        if (polygon.empty()){
            break;
        }
        std::vector<glm::vec3> output;
        auto previous = polygon.back();
        float previousDistance = planeDistance(plane, previous);
        for (const auto& current : polygon){
            const float currentDistance = planeDistance(plane, current);
            const bool previousInside = previousDistance >= -planeTolerance,
                       currentInside = currentDistance >= -planeTolerance;
            if (previousInside != currentInside){
                // Clip against the conservative, planeTolerance-expanded half-space.
                const float denominator = previousDistance - currentDistance;
                if (std::abs(denominator) > 1e-12f){
                    output.push_back(glm::mix(
                        previous, current,
                        std::clamp((previousDistance + planeTolerance) / denominator, 0.0f, 1.0f)));
                }
            }
            if (currentInside){
                output.push_back(current);
            }
            previous = current;
            previousDistance = currentDistance;
        }
        polygon = std::move(output);
    }
    return polygon;
}

// Validates cells and rectangular openings before replacing the visibility graph.
void PortalGraph::build(std::vector<PortalCell> cells, const std::vector<Doorway>& doors,
                        std::string exterior){
    const auto ids = validateCells(cells, exterior);
    std::vector<Portal> portals;
    for (const auto& door : doors){
        const auto portal = createPortal(door, cells, ids);
        portals.push_back(portal);
        portals.push_back({portal.to, portal.from, portal.corners, -portal.forwardPlane});
    }
    m_cells = std::move(cells);
    m_portals = std::move(portals);
    m_exterior = std::move(exterior);
}

// Collects visibility volumes through door sequences from a live or frozen eye.
PortalResult PortalGraph::collect(const glm::vec3& eye, const glm::mat4* viewProjection) const{
    PortalResult result;
    if (m_cells.empty()){
        return result;
    }
    std::size_t origin = m_cells.size();
    for (std::size_t i = 0; i < m_cells.size(); ++i){
        const auto& box = m_cells[i].bounds;
        if (contains(box, eye)){
            origin = i;
            break;
        }
    }
    const auto fallback = boundaryFallback(m_cells, m_portals, eye, origin);
    if (!fallback.empty()){
        result.origin = fallback;
        return result;
    }
    if (origin == m_cells.size() && m_exterior.empty()){
        result.origin = "outside fallback";
        return result;
    }
    ClipVolume initial;
    if (viewProjection){
        const Frustum frustum(*viewProjection);
        initial.planes.assign(frustum.getPlanes().begin(), frustum.getPlanes().end());
    }
    result.active = true;
    result.origin = origin == m_cells.size() ? m_exterior : m_cells[origin].id;
    std::vector<bool> path(m_cells.size() + 1, false);
    unsigned paths = 0;
    bool overflow = false;
    // Traverses connected rooms with the exterior as an additional graph region.
    std::function<void(std::size_t, const ClipVolume&)> visit = [&](std::size_t cell,
                                                                    const ClipVolume& volume){
        if (++paths > maxPortalPaths){
            overflow = true;
            return;
        }
        result.volumes[cell == m_cells.size() ? m_exterior : m_cells[cell].id].push_back(volume);
        path[cell] = true;
        for (const auto& portal : m_portals){
            if (portal.from == cell && !path[portal.to]){
                ++result.portalsTested;
                if (planeDistance(portal.forwardPlane, eye) > planeTolerance){
                    continue;
                }
                auto polygon = volume.clip({portal.corners.begin(), portal.corners.end()});
                if (polygon.size() < 3){
                    continue;
                }
                const auto child = throughPortal(volume, portal, polygon, eye);
                visit(portal.to, child);
                if (overflow){
                    break;
                }
            }
        }
        path[cell] = false;
    };
    visit(origin, initial);
    if (overflow){
        result.active = false;
        result.volumes.clear();
        result.origin = "path limit fallback";
    }
    return result;
}
} // namespace vr
