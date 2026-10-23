#pragma once
#include "Geometry.h"

namespace vr{
class GeometryFactory final{
public:
    // Builds the textured ground from explicit vertex arrays.
    static std::shared_ptr<Geometry> createGround();

    // Builds a box with separate face normals and texture coordinates.
    static std::shared_ptr<Geometry> createBox();

    // Builds a sphere from the requested slice and stack counts.
    static std::shared_ptr<Geometry> createSphere(unsigned slices, unsigned stacks);

    // Builds a torus with indexed triangles and smooth vertex normals.
    static std::shared_ptr<Geometry> createTorus(unsigned segments, unsigned sides);

    // Builds the two-triangle panel used to demonstrate alpha testing.
    static std::shared_ptr<Geometry> createAlphaTestPanel();
};
} // namespace vr
