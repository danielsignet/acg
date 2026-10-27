#include <vr/Geometry.h>
#include <array>
#include <cmath>
#include <limits>

namespace vr{
// Generates UV tangents, splitting shared vertices where mirrored charts meet.
void Geometry::buildTangents(std::vector<Vertex>& vertices, std::vector<unsigned>& indices){
    const auto missing = std::numeric_limits<unsigned>::max();
    std::vector<std::array<unsigned, 2>> charts(vertices.size(),{missing, missing});
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0)),
        bitangents(vertices.size(), glm::vec3(0));
    for (std::size_t face = 0; face < indices.size(); face += 3){
        const auto a = vertices[indices[face]], b = vertices[indices[face + 1]],
                   c = vertices[indices[face + 2]];
        const auto edge1 = b.position - a.position, edge2 = c.position - a.position;
        const auto uv1 = b.texCoord - a.texCoord, uv2 = c.texCoord - a.texCoord;
        const float determinant = uv1.x * uv2.y - uv1.y * uv2.x;
        const float area = glm::length(glm::cross(edge1, edge2));
        if (std::abs(determinant) < 1e-12f || area < 1e-20f){
            continue;
        }
        const auto tangent = (edge1 * uv2.y - edge2 * uv1.y) / determinant;
        const auto bitangent = (edge2 * uv1.x - edge1 * uv2.x) / determinant;
        for (unsigned corner = 0; corner < 3; ++corner){
            auto& index = indices[face + corner];
            auto& chart = charts[index];
            const unsigned side = determinant < 0 ? 0 : 1;
            if (chart[side] == missing){
                if (chart[1 - side] == missing){
                    chart[side] = index;
                } else{
                    chart[side] = static_cast<unsigned>(vertices.size());
                    vertices.push_back(vertices[index]);
                    tangents.emplace_back(0.0f);
                    bitangents.emplace_back(0.0f);
                }
            }
            index = chart[side];
            tangents[index] += tangent * area;
            bitangents[index] += bitangent * area;
        }
    }
    for (std::size_t i = 0; i < vertices.size(); ++i){
        const auto normal = vertices[i].normal;
        const float normalLength = glm::dot(normal, normal);
        if (normalLength < 1e-20f){
            continue;
        }
        const auto projected = tangents[i] - normal * glm::dot(normal, tangents[i]) / normalLength;
        const float length = glm::length(projected);
        if (std::isfinite(length) && length > 1e-20f){
            const float sign =
                glm::dot(glm::cross(normal, projected), bitangents[i]) < 0 ? -1.0f : 1.0f;
            vertices[i].tangent = glm::vec4(projected / length, sign);
        }
    }
}
} // namespace vr
