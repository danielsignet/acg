#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <vr/PortalVisibility.h>

namespace vr{
namespace{
constexpr float epsilon = .0001f;

float distance(const glm::vec4& plane, const glm::vec3& point){
    return glm::dot(glm::vec3(plane), point) + plane.w;
}

bool contains(const BoundingBox& box, const glm::vec3& point, float margin = epsilon){
    return glm::all(glm::greaterThan(point, box.getMinimum() + glm::vec3(margin))) &&
           glm::all(glm::lessThan(point, box.getMaximum() - glm::vec3(margin)));
}
} // namespace

// Tests bounds conservatively against a clipped visibility volume.
bool ClipVolume::intersects(const BoundingBox& box) const{
    if (!box.valid()){
        return true;
    }
    for (const auto& plane : planes){
        if (distance(plane, box.getCenter()) +
                glm::dot(glm::abs(glm::vec3(plane)), box.getExtent()) <
            -epsilon){
            return false;
        }
    }
    return true;
}

std::vector<glm::vec3> ClipVolume::clip(std::vector<glm::vec3> polygon) const{
    for (const auto& plane : planes){
        if (polygon.empty()){
            break;
        }
        std::vector<glm::vec3> output;
        auto previous = polygon.back();
        float a = distance(plane, previous);
        for (const auto& current : polygon){
            const float b = distance(plane, current);
            const bool inA = a >= -epsilon, inB = b >= -epsilon;
            if (inA != inB){
                // Clip against the conservative, epsilon-expanded half-space.
                const float denominator = a - b;
                if (std::abs(denominator) > 1e-12f){
                    output.push_back(glm::mix(previous, current,
                                              std::clamp((a + epsilon) / denominator, 0.0f, 1.0f)));
                }
            }
            if (inB){
                output.push_back(current);
            }
            previous = current;
            a = b;
        }
        polygon = std::move(output);
    }
    return polygon;
}

void PortalGraph::build(std::vector<PortalCell> cells, const std::vector<Doorway>& doors,
                        std::string exterior){
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
            if (glm::all(glm::greaterThan(overlap, glm::vec3(epsilon)))){
                throw std::invalid_argument("Overlapping visibility cells");
            }
        }
    }
    if (!exterior.empty() && !ids.emplace(exterior, cells.size()).second){
        throw std::invalid_argument("Exterior duplicates a room");
    }
    std::vector<Portal> portals;
    for (const auto& door : doors){
        if (!ids.count(door.from) || !ids.count(door.to) || door.from == door.to ||
            !door.opening.valid()){
            throw std::invalid_argument("Invalid portal endpoints or opening");
        }
        const auto from = ids.at(door.from), to = ids.at(door.to);
        const auto size = door.opening.getMaximum() - door.opening.getMinimum();
        int axis = -1;
        for (int a = 0; a < 3; ++a){
            if (size[a] < epsilon){
                if (axis != -1){
                    throw std::invalid_argument("Degenerate portal");
                }
                axis = a;
            }
        }
        if (axis == -1){
            throw std::invalid_argument("Portal must be planar");
        }
        const auto center = door.opening.getCenter();
        // Both finite endpoints must share the same face, with the opening inside it.
        for (auto endpoint : {from, to}){
            if (endpoint == cells.size()){
                continue;
            }
            const auto& box = cells[endpoint].bounds;
            if (std::min(std::abs(center[axis] - box.getMinimum()[axis]),
                         std::abs(center[axis] - box.getMaximum()[axis])) > epsilon){
                throw std::invalid_argument("Portal is not on a cell face");
            }
            for (int a = 0; a < 3; ++a){
                if (a != axis && (door.opening.getMinimum()[a] < box.getMinimum()[a] - epsilon ||
                                  door.opening.getMaximum()[a] > box.getMaximum()[a] + epsilon)){
                    throw std::invalid_argument("Portal extends outside its cell face");
                }
            }
        }
        const float delta = from < cells.size()
                                ? center[axis] - cells[from].bounds.getCenter()[axis]
                                : cells[to].bounds.getCenter()[axis] - center[axis];
        glm::vec3 normal(0);
        normal[axis] = delta > 0 ? 1.0f : -1.0f;
        if (to < cells.size() && from < cells.size() &&
            (cells[to].bounds.getCenter()[axis] - center[axis]) * normal[axis] <= 0){
            throw std::invalid_argument("Portal cells must be on opposite sides");
        }
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        std::array<glm::vec3, 4> points;
        for (unsigned i = 0; i < 4; ++i){
            auto p = center;
            p[u] = (i == 1 || i == 2) ? door.opening.getMaximum()[u] : door.opening.getMinimum()[u];
            p[v] = i >= 2 ? door.opening.getMaximum()[v] : door.opening.getMinimum()[v];
            points[i] = p;
        }
        const glm::vec4 plane(normal, -glm::dot(normal, center));
        portals.push_back({from, to, points, plane});
        portals.push_back({to, from, points, -plane});
    }
    m_cells = std::move(cells);
    m_portals = std::move(portals);
    m_exterior = std::move(exterior);
}

PortalResult PortalGraph::collect(const glm::vec3& eye, const glm::mat4* vp) const{
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
    // Keep geometry conservatively when the fly camera enters a wall or cell boundary.
    for (const auto& cell : m_cells){
        if (!contains(cell.bounds, eye, -.13f) || contains(cell.bounds, eye, .13f)){
            continue;
        }
        bool inDoor = false;
        for (const auto& portal : m_portals){
            if (std::abs(distance(portal.forwardPlane, eye)) > .13f){
                continue;
            }
            BoundingBox opening;
            for (const auto& point : portal.corners){
                opening.expandBy(point);
            }
            inDoor = inDoor || contains(opening, eye, -.13f);
        }
        if (inDoor && origin == m_cells.size()){
            result.origin = "door boundary fallback";
            return result;
        }
        if (!inDoor){
            result.origin = "wall boundary fallback";
            return result;
        }
    }
    if (origin == m_cells.size() && m_exterior.empty()){
        result.origin = "outside fallback";
        return result;
    }
    ClipVolume initial;
    if (vp){
        const Frustum frustum(*vp);
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
        if (++paths > 256){
            overflow = true;
            return;
        }
        result.volumes[cell == m_cells.size() ? m_exterior : m_cells[cell].id].push_back(volume);
        path[cell] = true;
        for (const auto& portal : m_portals){
            if (portal.from == cell && !path[portal.to]){
                ++result.portalsTested;
                if (distance(portal.forwardPlane, eye) > epsilon){
                    continue;
                }
                auto polygon = volume.clip({portal.corners.begin(), portal.corners.end()});
                if (polygon.size() < 3){
                    continue;
                }
                ClipVolume child = volume;
                child.planes.push_back(portal.forwardPlane);
                glm::vec3 center(0);
                for (auto p : polygon){
                    center += p;
                }
                center /= float(polygon.size());
                for (std::size_t edge = 0; edge < polygon.size(); ++edge){
                    auto normal =
                        glm::cross(polygon[edge] - eye, polygon[(edge + 1) % polygon.size()] - eye);
                    const float length = glm::length(normal);
                    if (length < 1e-7f){
                        continue;
                    }
                    normal /= length;
                    glm::vec4 plane(normal, -glm::dot(normal, eye));
                    if (distance(plane, center) < 0){
                        plane = -plane;
                    }
                    child.planes.push_back(plane);
                }
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
