#pragma once
#include "Transform.h"

namespace vr{
// A textured XY polygon that faces each observer while retaining its local Y axis.
// Facing is resolved per traversal path; the shared scene graph is never rotated.
class Billboard final : public Transform{
public:
    using Transform::Transform;
    void accept(NodeVisitor& visitor) override { visitor.apply(*this); }
    glm::mat4 traversalMatrix(const glm::mat4& parent,const glm::vec3& eye) const override{
        return facingMatrix(parent,eye);
    }
    glm::mat4 facingMatrix(const glm::mat4& parent, const glm::vec3& eye) const;
    BoundingBox computeBound() const override;
};
} // namespace vr
