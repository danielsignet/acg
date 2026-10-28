#pragma once
#include "Group.h"
#include <cstdint>

namespace vr{
class Transform : public Group{
public:
    using Group::Group;

    // Dispatches this concrete node type to the visitor.
    void accept(NodeVisitor& visitor) override{
        visitor.apply(*this);
    }

    // Replaces the local transformation without modifying child geometry.
    void setMatrix(const glm::mat4& matrix){
        if (m_matrix == matrix){
            return;
        }
        m_matrix = matrix;
        ++m_revision;
        m_worlds.clear();
        dirtyBound();
    }

    // Returns the local transformation accumulated during traversal.
    const glm::mat4& getMatrix() const{
        return m_matrix;
    }

    std::uint64_t getMatrixRevision() const{
        return m_revision;
    }

    // The parent matrix is part of the key, so shared DAG nodes retain distinct placements.
    glm::mat4 getWorldMatrix(const glm::mat4& parent) const;

    // Transforms the union of child bounds into the parent coordinate system.
    BoundingBox computeBound() const override;

private:
    glm::mat4 m_matrix{1.0f};
    std::uint64_t m_revision = 1;
    mutable std::vector<std::pair<glm::mat4, glm::mat4>> m_worlds;
};
} // namespace vr
