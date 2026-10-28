#pragma once
#include "Node.h"
#include "NodeVisitor.h"
#include "StreamBuffer.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace vr{
struct SkinningPose;

struct SkinWeights{
    static constexpr unsigned Count = 12;
    std::array<unsigned, Count> joints{};
    std::array<float, Count> weights{};
};

struct Vertex{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent{0};
    SkinWeights skin;
};

// Render attributes stay full precision; CPU-only joint weights never enter this layout.
struct GpuVertex{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;
};

static_assert(sizeof(GpuVertex) == 48, "Unexpected GPU vertex packing");

class Geometry : public Node{
public:
    struct SurfaceTransform{
        glm::mat3 normal;
        float tangentSign;
    };

    using Node::Node;

    // Releases the OpenGL buffers owned by this mesh.
    ~Geometry() override;

    // Dispatches this concrete node type to the visitor.
    void accept(NodeVisitor& visitor) override{
        visitor.apply(*this);
    }

    // Validates and stores local vertex attributes and triangle indices.
    void setData(std::vector<glm::vec3> vertices, std::vector<unsigned> indices,
                 std::vector<glm::vec3> normals, std::vector<glm::vec2> texCoords ={},
                 std::vector<glm::vec4> tangents ={}, std::vector<SkinWeights> skin ={});

    // Retains the undeformed control mesh and attaches its evaluated palette.
    void setSkin(std::shared_ptr<SkinningPose> skin);
    // Updates positions and tangent frames once per evaluated skeleton pose.
    std::size_t updateSkin();

    std::size_t getSkinInfluenceCount() const{
        return m_skinInfluences;
    }

    std::uint64_t getAppliedSkinRevision() const{
        return m_skinRevision;
    }

    // Creates independent deformable output while sharing immutable rest vertices.
    std::shared_ptr<Geometry> instantiateSkin(std::shared_ptr<SkinningPose> pose) const;

    // Shares the palette with in-memory subdivision variants.
    const std::shared_ptr<SkinningPose>& getSkin() const{
        return m_skin;
    }

    // Returns the undeformed control mesh for subdivision.
    const std::vector<Vertex>& getRestVertices() const{
        return m_restVertices ? *m_restVertices : m_vertices;
    }

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

    // Streams model matrices and draws this mesh once for all supplied instances.
    void drawInstanced(const std::vector<glm::mat4>& models, bool surface = false) const;
    SurfaceTransform getSurfaceTransform(const glm::mat4& model) const;

    // Finds the closest BVH hit whose interpolated UV passes the optional surface filter.
    std::optional<float>
    intersectRay(const glm::vec3& origin, const glm::vec3& direction, float maximum,
                 const std::function<bool(const glm::vec2&)>& accepts ={}) const;

    // Computes the local bound from the stored vertex positions.
    BoundingBox computeBound() const override;

private:
    // Generates UV tangents, splitting shared vertices where mirrored charts meet.
    static void buildTangents(std::vector<Vertex>& vertices, std::vector<unsigned>& indices);

    // Binds the VAO and uploads changed vertices and indices for either draw path.
    void bind() const;
    void bindAttributes(GLuint buffer) const;

    // Builds a balanced hierarchy over triangle bounds without issuing OpenGL calls.
    void buildRayTree() const;
    // Refits picking bounds after deformation while retaining the tree topology.
    void refitRayTree() const;

    struct RayNode{
        BoundingBox bounds;
        unsigned first = 0, count = 0, left = 0, right = 0;
    };

    std::vector<Vertex> m_vertices;
    std::vector<unsigned> m_indices;
    mutable GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    mutable StreamBuffer m_vertexStream, m_instanceStream, m_normalStream;
    mutable std::vector<GpuVertex> m_uploadVertices;
    mutable std::vector<std::pair<glm::mat4, SurfaceTransform>> m_surfaceTransforms;
    mutable bool m_dirty = true;
    mutable bool m_vertexDirty = false, m_rayDirty = false;
    std::shared_ptr<SkinningPose> m_skin;
    std::shared_ptr<const std::vector<Vertex>> m_restVertices;
    std::uint64_t m_skinRevision = 0;
    std::size_t m_skinInfluences = 0;
    mutable std::vector<RayNode> m_rayNodes;
    mutable std::vector<unsigned> m_rayTriangles;
};
} // namespace vr
