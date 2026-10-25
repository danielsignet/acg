#include <vr/Transform.h>

namespace vr{
// Transforms the union of child bounds into the parent coordinate system.
BoundingBox Transform::computeBound() const{
    return Group::computeBound().transformed(m_matrix);
}
} // namespace vr
