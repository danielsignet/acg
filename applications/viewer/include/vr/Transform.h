#pragma once
#include "Group.h"

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
        m_matrix = matrix;
        dirtyBound();
    }

    // Returns the local transformation accumulated during traversal.
    const glm::mat4& getMatrix() const{
        return m_matrix;
    }

    BoundingBox computeBound() const override;

private:
    glm::mat4 m_matrix{1.0f};
};
} // namespace vr
