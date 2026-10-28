#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vr/Group.h>
#include <vr/SkeletalAnimation.h>
#include <vr/Subdivision.h>
#include <vr/Transform.h>

namespace vr{
namespace{
// Blends joint influences with the same Loop weights as positions.
SkinWeights blendSkin(const std::vector<std::pair<SkinWeights, float>>& sources){
    std::map<unsigned, float> weights;
    for (const auto& [skin, factor] : sources){
        for (unsigned slot = 0; slot < SkinWeights::Count; ++slot){
            if (skin.weights[slot] > 0){
                weights[skin.joints[slot]] += skin.weights[slot] * factor;
            }
        }
    }
    std::vector<std::pair<unsigned, float>> ordered(weights.begin(), weights.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });
    SkinWeights result;
    float total = 0;
    for (unsigned i = 0; i < std::min<std::size_t>(SkinWeights::Count, ordered.size()); ++i){
        result.joints[i] = ordered[i].first;
        result.weights[i] = ordered[i].second;
        total += ordered[i].second;
    }
    if (total > 0){
        for (auto& weight : result.weights){
            weight /= total;
        }
    }
    return result;
}
} // namespace

// Builds a separate model hierarchy with one Loop subdivision level per shared mesh.
std::shared_ptr<Node> Subdivision::create(const std::shared_ptr<Node>& asset){
    std::map<const Node*, std::shared_ptr<Node>> copies;
    const std::function<std::shared_ptr<Node>(const std::shared_ptr<Node>&)> clone =
        [&](const auto& node){
            if (const auto found = copies.find(node.get()); found != copies.end()){
                return found->second;
            }
            std::shared_ptr<Node> copy;
            if (auto geometry = std::dynamic_pointer_cast<Geometry>(node)){
                auto mesh = std::make_shared<Geometry>(node->getName());
                apply(*mesh, refine({geometry->getRestVertices(), geometry->getIndices()}));
                mesh->setSkin(geometry->getSkin());
                copy = mesh;
            } else if (auto group = std::dynamic_pointer_cast<Group>(node)){
                std::shared_ptr<Group> branch;
                if (auto transform = std::dynamic_pointer_cast<Transform>(node)){
                    auto transformed = std::make_shared<Transform>(node->getName());
                    transformed->setMatrix(transform->getMatrix());
                    branch = transformed;
                } else{
                    branch = std::make_shared<Group>(node->getName());
                }
                copies.emplace(node.get(), branch);
                for (const auto& child : group->getChildren()){
                    branch->addChild(clone(child));
                }
                copy = branch;
            } else{
                throw std::runtime_error(
                    "Subdivision requires a model containing meshes and transforms");
            }
            copy->setState(node->getState());
            copy->setVisible(node->getVisible());
            copy->setRoomId(node->getRoomId());
            copy->setCullingEnabled(node->getCullingEnabled());
            if (!std::dynamic_pointer_cast<SkeletalAnimation>(node->getUpdateCallback())){
                copy->setUpdateCallback(node->getUpdateCallback());
            }
            copies[node.get()] = copy;
            return copy;
        };
    auto refined = clone(asset);
    auto anchored = std::make_shared<Transform>("subdivision support");
    glm::mat4 placement(1);
    if (!std::dynamic_pointer_cast<SkeletalAnimation>(asset->getUpdateCallback()) &&
        !std::dynamic_pointer_cast<JointAnimationCallback>(asset->getUpdateCallback()) &&
        asset->getBound().valid() && refined->getBound().valid()){
        placement[3].y = asset->getBound().getMinimum().y - refined->getBound().getMinimum().y;
    }
    anchored->setMatrix(placement);
    anchored->addChild(refined);
    return anchored;
}

// Refines triangle connectivity and recomputes smooth area-weighted normals.
Subdivision::Mesh Subdivision::refine(const Mesh& mesh){
    Mesh control;
    std::map<std::tuple<float, float, float>, unsigned> positions;
    std::vector<unsigned> welded;
    for (const auto& vertex : mesh.vertices){
        const auto p = vertex.position;
        const auto [found, inserted] = positions.emplace(
            std::make_tuple(p.x, p.y, p.z), static_cast<unsigned>(control.vertices.size()));
        if (inserted){
            control.vertices.push_back({p,{},{},{}, vertex.skin});
        }
        welded.push_back(found->second);
    }
    std::map<std::pair<unsigned, unsigned>, unsigned> edges;
    const auto edge = [](unsigned a, unsigned b){
        return std::make_pair(std::min(a, b), std::max(a, b));
    };
    for (const auto index : mesh.indices){
        control.indices.push_back(welded[index]);
    }
    for (std::size_t i = 0; i < control.indices.size(); i += 3){
        for (unsigned j = 0; j < 3; ++j){
            edges.emplace(edge(control.indices[i + j], control.indices[i + (j + 1) % 3]), 0);
        }
    }
    unsigned next = static_cast<unsigned>(control.vertices.size());
    for (auto& item : edges){
        item.second = next++;
    }
    const auto refined = refineControl(control);
    Mesh result;
    std::map<std::tuple<unsigned, float, float>, unsigned> corners;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3){
        const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const std::array<unsigned, 6> ids{
            {welded[a], welded[b], welded[c], edges.at(edge(welded[a], welded[b])),
             edges.at(edge(welded[b], welded[c])), edges.at(edge(welded[c], welded[a]))}};
        const auto uvA = mesh.vertices[a].texCoord, uvB = mesh.vertices[b].texCoord,
                   uvC = mesh.vertices[c].texCoord;
        const std::array<glm::vec2, 6> uv{
            {uvA, uvB, uvC, (uvA + uvB) * 0.5f, (uvB + uvC) * 0.5f, (uvC + uvA) * 0.5f}};
        for (const unsigned local :{0u, 3u, 5u, 1u, 4u, 3u, 2u, 5u, 4u, 3u, 4u, 5u}){
            const auto [found, inserted] =
                corners.emplace(std::make_tuple(ids[local], uv[local].x, uv[local].y),
                                static_cast<unsigned>(result.vertices.size()));
            if (inserted){
                auto vertex = refined.vertices[ids[local]];
                vertex.texCoord = uv[local];
                result.vertices.push_back(vertex);
            }
            result.indices.push_back(found->second);
        }
    }
    return result;
}

// Applies Loop weights to welded geometric vertices independently of texture seams.
Subdivision::Mesh Subdivision::refineControl(const Mesh& mesh){
    if (mesh.indices.empty()){
        return mesh;
    }

    struct Edge{
        std::vector<unsigned> opposite;
        unsigned midpoint = 0;
    };

    std::map<std::pair<unsigned, unsigned>, Edge> edges;
    std::vector<std::set<unsigned>> neighbors(mesh.vertices.size()), boundary(mesh.vertices.size());
    const auto key = [](unsigned a, unsigned b){
        return std::make_pair(std::min(a, b), std::max(a, b));
    };
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3){
        for (unsigned j = 0; j < 3; ++j){
            const auto a = mesh.indices[i + j], b = mesh.indices[i + (j + 1) % 3];
            edges[key(a, b)].opposite.push_back(mesh.indices[i + (j + 2) % 3]);
            neighbors[a].insert(b);
            neighbors[b].insert(a);
        }
    }
    for (const auto& [edge, data] : edges){
        // Treat open and nonmanifold edges as creases rather than joining unrelated surfaces.
        if (data.opposite.size() != 2){
            boundary[edge.first].insert(edge.second);
            boundary[edge.second].insert(edge.first);
        }
    }
    Mesh result;
    result.vertices = mesh.vertices;
    result.vertices.reserve(mesh.vertices.size() + edges.size());
    for (unsigned i = 0; i < mesh.vertices.size(); ++i){
        const auto& ring = boundary[i].empty() ? neighbors[i] : boundary[i];
        if (ring.empty()){
            continue;
        }
        if (!boundary[i].empty() && ring.size() != 2){
            continue;
        }
        const float weight = !boundary[i].empty() ? 0.125f
                             : ring.size() == 3   ? 3.0f / 16
                                                  : 3.0f / (8 * ring.size());
        auto& vertex = result.vertices[i];
        vertex.position *= 1 - ring.size() * weight;
        vertex.texCoord *= 1 - ring.size() * weight;
        std::vector<std::pair<SkinWeights, float>> skin{
            {mesh.vertices[i].skin, 1 - ring.size() * weight}};
        for (const auto adjacent : ring){
            vertex.position += mesh.vertices[adjacent].position * weight;
            vertex.texCoord += mesh.vertices[adjacent].texCoord * weight;
            skin.push_back({mesh.vertices[adjacent].skin, weight});
        }
        vertex.skin = blendSkin(skin);
    }
    for (auto& [edge, data] : edges){
        const auto& a = mesh.vertices[edge.first];
        const auto& b = mesh.vertices[edge.second];
        const float weight = data.opposite.size() == 2 ? 0.375f : 0.5f;
        Vertex vertex{(a.position + b.position) * weight,{}, (a.texCoord + b.texCoord) * weight};
        std::vector<std::pair<SkinWeights, float>> skin{{a.skin, weight},{b.skin, weight}};
        if (data.opposite.size() == 2){
            for (const auto opposite : data.opposite){
                vertex.position += mesh.vertices[opposite].position * 0.125f;
                vertex.texCoord += mesh.vertices[opposite].texCoord * 0.125f;
                skin.push_back({mesh.vertices[opposite].skin, 0.125f});
            }
        }
        vertex.skin = blendSkin(skin);
        data.midpoint = static_cast<unsigned>(result.vertices.size());
        result.vertices.push_back(vertex);
    }
    result.indices.reserve(mesh.indices.size() * 4);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3){
        const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const auto ab = edges.at(key(a, b)).midpoint, bc = edges.at(key(b, c)).midpoint,
                   ca = edges.at(key(c, a)).midpoint;
        result.indices.insert(result.indices.end(),{a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
    }
    for (auto& vertex : result.vertices){
        vertex.normal = glm::vec3(0);
    }
    for (std::size_t i = 0; i < result.indices.size(); i += 3){
        auto& a = result.vertices[result.indices[i]];
        auto& b = result.vertices[result.indices[i + 1]];
        auto& c = result.vertices[result.indices[i + 2]];
        const auto normal = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += normal;
        b.normal += normal;
        c.normal += normal;
    }
    for (auto& vertex : result.vertices){
        vertex.normal =
            glm::length(vertex.normal) > 1e-8f ? glm::normalize(vertex.normal) : glm::vec3(0, 1, 0);
    }
    return result;
}

// Replaces one mesh through the normal geometry upload and bound-invalidation path.
void Subdivision::apply(Geometry& geometry, const Mesh& mesh){
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> coordinates;
    std::vector<SkinWeights> skin;
    for (const auto& vertex : mesh.vertices){
        positions.push_back(vertex.position);
        normals.push_back(vertex.normal);
        coordinates.push_back(vertex.texCoord);
        skin.push_back(vertex.skin);
    }
    geometry.setData(std::move(positions), mesh.indices, std::move(normals), std::move(coordinates),
                     {}, std::move(skin));
}
} // namespace vr
