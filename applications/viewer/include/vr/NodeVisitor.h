#pragma once

namespace vr{
class Node;
class Group;
class Transform;
class Geometry;
class Light;
class Camera;
class LOD;
class ModelInstance;
class Billboard;

class NodeVisitor{
public:
    // Allows derived objects to release their owned resources safely.
    virtual ~NodeVisitor() = 0;

    // Processes this concrete node type through visitor dispatch.
    virtual void apply(Group& node);

    // Processes this concrete node type through visitor dispatch.
    virtual void apply(Transform& node);

    // Processes this concrete node type through visitor dispatch.
    virtual void apply(Geometry& node);

    // Processes this concrete node type through visitor dispatch.
    virtual void apply(Light& node);

    // Processes this concrete node type through visitor dispatch.
    virtual void apply(Camera& node);

    // Processes this concrete node type through visitor dispatch.
    virtual void apply(LOD& node);
    virtual void apply(ModelInstance& node);
    virtual void apply(Billboard& node);

protected:
    // Visits a snapshot of the children in depth-first order.
    void traverse(Group& group);
};
} // namespace vr
