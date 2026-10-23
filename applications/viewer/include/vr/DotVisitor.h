#pragma once
#include "SceneGraph.h"
#include <ostream>

namespace vr{
// Exports graph structure, including shared subgraphs and inactive LOD levels.
// IDs are assigned by object identity in traversal order, never by node name.
class DotVisitor final : public NodeVisitor{
public:
    // Binds graph serialization to the supplied output stream.
    explicit DotVisitor(std::ostream& output) : m_output(output){
    }

    // Exports each unique node once while retaining all parent-child edges.
    void write(Node& root);

    // Writes a complete DOT document to the requested file.
    static void save(Node& root, const std::filesystem::path& path);

    // Exports this group and every edge to its children.
    void apply(Group& node) override;

    // Exports the transform without changing its matrix.
    void apply(Transform& node) override;

    // Exports the mesh name and triangle count.
    void apply(Geometry& node) override;

    // Exports the light as a graph leaf.
    void apply(Light& node) override;

    // Exports the camera as a graph leaf.
    void apply(Camera& node) override;

    // Exports every detail branch with its distance range.
    void apply(LOD& node) override;

private:
    // Escapes node-label characters that have special meaning in DOT.
    static std::string escapeLabel(const std::string& value);

    // Formats a LOD boundary independently of the system locale.
    static std::string formatDistance(float distance);

    // Assigns a stable traversal identifier based on object identity.
    std::size_t id(const Node& node);

    // Records a first visit before processing this node.
    bool enter(Node& node, const std::string& type, const char* color,
               const std::string& detail ={});

    // Exports a group and expands its children only on the first visit.
    void group(Group& node, const std::string& type, const char* color);
    std::ostream& m_output;
    std::map<const Node*, std::size_t> m_ids;
    std::set<const Node*> m_written;
};

} // namespace vr
