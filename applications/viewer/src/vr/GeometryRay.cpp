#include <vr/Geometry.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace vr{
namespace{
// Clips a forward ray against a local-space bounding box before testing triangles.
bool intersects(const BoundingBox& bound, glm::vec3 origin, glm::vec3 direction, float maximum){
    float minimum = 0;
    for (unsigned axis = 0; axis < 3; ++axis){
        const float low = bound.getMinimum()[axis], high = bound.getMaximum()[axis];
        if (std::abs(direction[axis]) < 1e-8f){
            if (origin[axis] < low || origin[axis] > high){
                return false;
            }
        } else{
            const float a = (low - origin[axis]) / direction[axis],
                        b = (high - origin[axis]) / direction[axis];
            minimum = std::max(minimum, std::min(a, b));
            maximum = std::min(maximum, std::max(a, b));
            if (maximum < minimum){
                return false;
            }
        }
    }
    return true;
}
} // namespace

// Builds a balanced hierarchy over triangle bounds without issuing OpenGL calls.
void Geometry::buildRayTree() const{
    const auto count = static_cast<unsigned>(m_indices.size() / 3);
    std::vector<BoundingBox> triangleBounds(count);
    for (unsigned i = 0; i < count; ++i){
        for (unsigned corner = 0; corner < 3; ++corner){
            triangleBounds[i].expandBy(m_vertices[m_indices[i * 3 + corner]].position);
        }
    }
    m_rayTriangles.resize(count);
    std::iota(m_rayTriangles.begin(), m_rayTriangles.end(), 0u);
    m_rayNodes.reserve(count / 2 + 1);
    using NodeIndex = unsigned;
    const std::function<NodeIndex(unsigned, unsigned)> build = [&](unsigned first, unsigned size){
        RayNode node;
        node.first = first;
        node.count = size;
        BoundingBox centers;
        for (unsigned i = first; i < first + size; ++i){
            node.bounds.expandBy(triangleBounds[m_rayTriangles[i]]);
            centers.expandBy(triangleBounds[m_rayTriangles[i]].getCenter());
        }
        const auto index = static_cast<unsigned>(m_rayNodes.size());
        m_rayNodes.push_back(node);
        if (size > 8){
            const auto extent = centers.getExtent();
            const unsigned axis = extent.x >= extent.y && extent.x >= extent.z ? 0
                                  : extent.y >= extent.z                       ? 1
                                                                               : 2;
            auto begin = m_rayTriangles.begin() + first;
            std::nth_element(begin, begin + size / 2, begin + size, [&](unsigned a, unsigned b){
                return triangleBounds[a].getCenter()[axis] < triangleBounds[b].getCenter()[axis];
            });
            const auto left = build(first, size / 2),
                       right = build(first + size / 2, size - size / 2);
            m_rayNodes[index].left = left;
            m_rayNodes[index].right = right;
            m_rayNodes[index].count = 0;
        }
        return index;
    };
    build(0, count);
    m_rayDirty = false;
}

// Refits picking bounds after deformation while retaining the tree topology.
void Geometry::refitRayTree() const{
    for (auto item = m_rayNodes.rbegin(); item != m_rayNodes.rend(); ++item){
        item->bounds ={};
        if (item->count){
            for (unsigned i = item->first; i < item->first + item->count; ++i){
                const auto first = m_rayTriangles[i] * 3;
                for (unsigned corner = 0; corner < 3; ++corner){
                    item->bounds.expandBy(m_vertices[m_indices[first + corner]].position);
                }
            }
        } else{
            item->bounds.expandBy(m_rayNodes[item->left].bounds);
            item->bounds.expandBy(m_rayNodes[item->right].bounds);
        }
    }
    m_rayDirty = false;
}

// Preserves world-space distance by not renormalizing the transformed ray direction.
std::optional<float>
Geometry::intersectRay(const glm::vec3& origin, const glm::vec3& direction, float maximum,
                       const std::function<bool(const glm::vec2&)>& accepts) const{
    if (m_indices.empty() || !intersects(getBound(), origin, direction, maximum)){
        return{};
    }
    if (m_rayNodes.empty()){
        buildRayTree();
    } else if (m_rayDirty){
        refitRayTree();
    }
    std::optional<float> closest;
    std::vector<unsigned> stack{0};
    while (!stack.empty()){
        const auto& node = m_rayNodes[stack.back()];
        stack.pop_back();
        if (!intersects(node.bounds, origin, direction, maximum)){
            continue;
        }
        if (!node.count){
            stack.push_back(node.left);
            stack.push_back(node.right);
            continue;
        }
        for (unsigned i = node.first; i < node.first + node.count; ++i){
            const auto triangle = m_rayTriangles[i] * 3;
            const auto a = m_vertices[m_indices[triangle]].position;
            const auto edge1 = m_vertices[m_indices[triangle + 1]].position - a,
                       edge2 = m_vertices[m_indices[triangle + 2]].position - a;
            const auto p = glm::cross(direction, edge2);
            const float determinant = glm::dot(edge1, p);
            if (std::abs(determinant) <= 1e-7f * glm::length(edge1) * glm::length(p)){
                continue;
            }
            const auto offset = origin - a;
            const float u = glm::dot(offset, p) / determinant;
            const auto q = glm::cross(offset, edge1);
            const float v = glm::dot(direction, q) / determinant;
            const float distance = glm::dot(edge2, q) / determinant;
            if (u >= 0 && v >= 0 && u + v <= 1 && distance > 0 && distance < maximum){
                if (accepts){
                    const auto coordinates =
                        m_vertices[m_indices[triangle]].texCoord * (1 - u - v) +
                        m_vertices[m_indices[triangle + 1]].texCoord * u +
                        m_vertices[m_indices[triangle + 2]].texCoord * v;
                    if (!accepts(coordinates)){
                        continue;
                    }
                }
                closest = maximum = distance;
            }
        }
    }
    return closest;
}
} // namespace vr
