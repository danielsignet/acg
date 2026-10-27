#pragma once
#include "Geometry.h"

namespace vr{
class Subdivision final{
public:
    // Builds a separate model hierarchy with one Loop subdivision level per shared mesh.
    static std::shared_ptr<Node> create(const std::shared_ptr<Node>& asset);

private:
    struct Mesh{
        std::vector<Vertex> vertices;
        std::vector<unsigned> indices;
    };

    // Refines triangle connectivity and recomputes smooth area-weighted normals.
    static Mesh refine(const Mesh& mesh);

    // Applies Loop weights to welded geometric vertices independently of texture seams.
    static Mesh refineControl(const Mesh& mesh);

    // Replaces one mesh through the normal geometry upload and bound-invalidation path.
    static void apply(Geometry& geometry, const Mesh& mesh);
};
} // namespace vr
