#include <cmath>
#include <glm/gtc/constants.hpp>
#include <stdexcept>
#include <vr/GeometryFactory.h>

namespace vr{
// Builds the textured ground from explicit vertex arrays.
std::shared_ptr<Geometry> GeometryFactory::createGround(){
    auto geometry = std::make_shared<Geometry>("hand-built ground");
    geometry->setData({{-16, 0, -12},{-16, 0, 12},{16, 0, 12},{16, 0, -12}},{0, 1, 2, 0, 2, 3},
                      {{0, 1, 0},{0, 1, 0},{0, 1, 0},{0, 1, 0}},
                      {{0, 0},{0, 8},{10, 8},{10, 0}});
    return geometry;
}

// Builds a box with separate face normals and texture coordinates.
std::shared_ptr<Geometry> GeometryFactory::createBox(){
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> textureCoordinates;
    std::vector<unsigned> indices;
    const glm::vec3 faceNormals[] ={{0, 0, 1},{1, 0, 0},{0, 0, -1},
                                     {-1, 0, 0},{0, 1, 0},{0, -1, 0}};
    const glm::vec3 faceTangents[] ={{1, 0, 0},{0, 0, -1},{-1, 0, 0},
                                      {0, 0, 1},{1, 0, 0},{1, 0, 0}};
    for (unsigned face = 0; face < 6; ++face){
        const auto bitangent = glm::cross(faceNormals[face], faceTangents[face]);
        const unsigned start = static_cast<unsigned>(positions.size());
        for (const auto& t : std::vector<glm::vec2>{{-1, -1},{1, -1},{1, 1},{-1, 1}}){
            positions.push_back((faceNormals[face] + faceTangents[face] * t.x + bitangent * t.y) *
                                0.5f);
            normals.push_back(faceNormals[face]);
            textureCoordinates.push_back(t * 0.5f + 0.5f);
        }
        for (unsigned i :{0u, 1u, 2u, 0u, 2u, 3u}){
            indices.push_back(start + i);
        }
    }
    auto geometry = std::make_shared<Geometry>("box");
    geometry->setData(positions, indices, normals, textureCoordinates);
    return geometry;
}

// Builds a sphere from the requested slice and stack counts.
std::shared_ptr<Geometry> GeometryFactory::createSphere(unsigned slices, unsigned stacks){
    if (slices < 3 || stacks < 2){
        throw std::invalid_argument("Sphere resolution");
    }
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> textureCoordinates;
    std::vector<unsigned> indices;
    for (unsigned y = 0; y <= stacks; ++y){
        for (unsigned x = 0; x <= slices; ++x){
            const float u = static_cast<float>(x) / slices;
            const float t = static_cast<float>(y) / stacks;
            const float phi = u * glm::two_pi<float>();
            const float theta = t * glm::pi<float>();
            glm::vec3 p{std::sin(theta) * std::cos(phi), std::cos(theta),
                        std::sin(theta) * std::sin(phi)};
            positions.push_back(p);
            normals.push_back(p);
            textureCoordinates.push_back({u, t});
        }
    }
    for (unsigned y = 0; y < stacks; ++y){
        for (unsigned x = 0; x < slices; ++x){
            unsigned a = y * (slices + 1) + x;
            unsigned b = a + slices + 1;
            if (y != 0){
                indices.insert(indices.end(),{a, a + 1, b});
            }
            if (y + 1 != stacks){
                indices.insert(indices.end(),{a + 1, b + 1, b});
            }
        }
    }
    auto geometry = std::make_shared<Geometry>("sphere");
    geometry->setData(positions, indices, normals, textureCoordinates);
    return geometry;
}

// Builds a torus with indexed triangles and smooth vertex normals.
std::shared_ptr<Geometry> GeometryFactory::createTorus(unsigned segments, unsigned sides){
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> textureCoordinates;
    std::vector<unsigned> indices;
    for (unsigned i = 0; i <= segments; ++i){
        for (unsigned j = 0; j <= sides; ++j){
            const float a = static_cast<float>(i) / segments * glm::two_pi<float>();
            const float b = static_cast<float>(j) / sides * glm::two_pi<float>();
            glm::vec3 normal{std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)};
            positions.push_back(glm::vec3(std::cos(a) * 1.3f, 0, std::sin(a) * 1.3f) +
                                0.38f * normal);
            normals.push_back(normal);
            textureCoordinates.push_back(
                {static_cast<float>(i) / segments, static_cast<float>(j) / sides});
        }
    }
    for (unsigned i = 0; i < segments; ++i){
        for (unsigned j = 0; j < sides; ++j){
            unsigned a = i * (sides + 1) + j;
            unsigned b = a + sides + 1;
            indices.insert(indices.end(),{a, a + 1, b, a + 1, b + 1, b});
        }
    }
    auto geometry = std::make_shared<Geometry>("animated torus");
    geometry->setData(positions, indices, normals, textureCoordinates);
    return geometry;
}

// Builds the two-triangle panel used to demonstrate alpha testing.
std::shared_ptr<Geometry> GeometryFactory::createAlphaTestPanel(){
    auto geometry = std::make_shared<Geometry>("alphatest panel");
    geometry->setData({{-1, 0, 0},{1, 0, 0},{1, 2.8f, 0},{-1, 2.8f, 0}},{0, 1, 2, 0, 2, 3},
                      {{0, 0, 1},{0, 0, 1},{0, 0, 1},{0, 0, 1}},
                      {{0, 0},{1, 0},{1, 1},{0, 1}});
    return geometry;
}
} // namespace vr
