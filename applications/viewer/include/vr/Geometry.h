#pragma once
#include "Node.h"
#include "NodeVisitor.h"

namespace vr{
struct Vertex{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

class Geometry : public Node{
public:
    using Node::Node;

    // Releases the OpenGL buffers owned by this mesh.
    ~Geometry() override;

    // Dispatches this concrete node type to the visitor.
    void accept(NodeVisitor& visitor) override{
        visitor.apply(*this);
    }

    // Validates and stores local vertex attributes and triangle indices.
    void setData(std::vector<glm::vec3> vertices, std::vector<unsigned> indices,
                 std::vector<glm::vec3> normals, std::vector<glm::vec2> texCoords ={});

    // Returns the mesh vertices in their local coordinate system.
    const std::vector<Vertex>& getVertices() const{
        return m_vertices;
    }

    // Returns the indices of the mesh triangles.
    const std::vector<unsigned>& getIndices() const{
        return m_indices;
    }

    // Returns the number of triangles represented by the index buffer.
    std::size_t getTriangleCount() const{
        return m_indices.size() / 3;
    }

    // Uploads changed mesh data and draws its indexed triangles.
    void draw() const;

    // Computes the local bound from the stored vertex positions.
    BoundingBox computeBound() const override;

private:
    std::vector<Vertex> m_vertices;
    std::vector<unsigned> m_indices;
    mutable GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    mutable bool m_dirty = true;
};
} // namespace vr
