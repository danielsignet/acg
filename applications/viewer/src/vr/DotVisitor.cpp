#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vr/DotVisitor.h>

namespace vr{
// Escapes node-label characters that have special meaning in DOT.
std::string DotVisitor::escapeLabel(const std::string& value){
    std::string result;
    for (char c : value){
        switch (c){
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            break;
        case '\t':
            result += "    ";
            break;
        default:
            result += c;
        }
    }
    return result;
}

// Formats a LOD boundary independently of the system locale.
std::string DotVisitor::formatDistance(float distance){
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(6) << distance;
    return text.str();
}

// Assigns a stable traversal identifier based on object identity.
std::size_t DotVisitor::id(const Node& node){
    return m_ids.emplace(&node, m_ids.size()).first->second;
}

// Records a first visit before processing this node.
bool DotVisitor::enter(Node& node, const std::string& type, const char* color,
                       const std::string& detail){
    if (!m_written.insert(&node).second){
        return false;
    }
    std::string label = type + "\n" + (node.getName().empty() ? "(unnamed)" : node.getName());
    if (!detail.empty()){
        label += "\n" + detail;
    }
    if (node.getState()){
        label += "\n[local state]";
    }
    if (node.getUpdateCallback()){
        label += "\n[update callback]";
    }
    m_output << "  n" << id(node) << " [label=\"" << escapeLabel(label) << "\", fillcolor=\""
             << color << "\"];\n";
    return true;
}

// Exports each unique node once while retaining all parent-child edges.
void DotVisitor::write(Node& root){
    m_ids.clear();
    m_written.clear();
    m_output << "digraph SceneGraph {\n"
                "  rankdir=TB;\n"
                "  node [shape=box, style=\"rounded,filled\", fontname=\"Arial\"];\n"
                "  edge [fontname=\"Arial\", fontsize=10];\n";
    root.accept(*this);
    m_output << "}\n";
    if (!m_output){
        throw std::runtime_error("Cannot write scene graph DOT");
    }
}

// Exports a group and expands its children only on the first visit.
void DotVisitor::group(Group& node, const std::string& type, const char* color){
    if (!enter(node, type, color)){
        return;
    }
    for (const auto& child : node.getChildren()){
        m_output << "  n" << id(node) << " -> n" << id(*child) << ";\n";
    }
    // Each unique node is expanded once, but every parent-child edge is retained.
    traverse(node);
}

// Exports this group and every edge to its children.
void DotVisitor::apply(Group& node){
    group(node, "Group", "#e2e8f0");
}

// Exports the transform without changing its matrix.
void DotVisitor::apply(Transform& node){
    group(node, "Transform", "#bfdbfe");
}

// Exports the mesh name and triangle count.
void DotVisitor::apply(Geometry& node){
    enter(node, "Geometry", "#bbf7d0", std::to_string(node.getTriangleCount()) + " triangles");
}

// Exports the light as a graph leaf.
void DotVisitor::apply(Light& node){
    enter(node, "Light", "#fef08a");
}

// Exports the camera as a graph leaf.
void DotVisitor::apply(Camera& node){
    enter(node, "Camera", "#ddd6fe");
}

// Exports every detail branch with its distance range.
void DotVisitor::apply(LOD& node){
    if (!enter(node, "LOD", "#fed7aa")){
        return;
    }
    const auto& children = node.getChildren();
    for (std::size_t i = 0; i < children.size(); ++i){
        const auto lower = i ? formatDistance(node.getRanges().at(i - 1)) : "0";
        const auto upper =
            i + 1 == children.size() ? "inf" : formatDistance(node.getRanges().at(i));
        m_output << "  n" << id(node) << " -> n" << id(*children[i]) << " [headlabel=\"[" << lower
                 << ", " << upper << ")\", labeldistance=2, labelangle=0];\n";
    }
    traverse(node);
}

// Writes a complete DOT document to the requested file.
void DotVisitor::save(Node& root, const std::filesystem::path& path){
    if (path.has_parent_path()){
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary);
    if (!output){
        throw std::runtime_error("Cannot open scene graph DOT: " + path.string());
    }
    DotVisitor(output).write(root);
    output.close();
    if (!output){
        throw std::runtime_error("Cannot save scene graph DOT: " + path.string());
    }
}
} // namespace vr
