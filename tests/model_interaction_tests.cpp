#include <vr/ModelInstance.h>
#include <vr/ModelPicker.h>
#include <vr/Subdivision.h>
#include <vr/Reader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace{
// Reports failed subdivision and picking contracts to CTest.
void check(bool value, const char* message){
    if (!value){
        throw std::runtime_error(message);
    }
}

// Collects the active meshes without creating any OpenGL objects.
vr::CullVisitor collect(vr::Node& root){
    vr::CullVisitor visitor;
    visitor.collect(root, glm::mat4(1),{0, 0, 8}, false);
    return visitor;
}

// Checks instance isolation, shared assets, restored bounds, normals and cached toggles.
void subdivision(const std::shared_ptr<vr::Node>& asset){
    auto first = vr::ModelInstance::create("first", asset),
         second = vr::ModelInstance::create("second", asset);
    auto root = std::make_shared<vr::Group>();
    root->addChild(first);
    root->addChild(second);
    auto original = collect(*first);
    const auto before = original.getRenderList();
    unsigned triangles = 0;
    for (const auto& item : before){
        triangles += static_cast<unsigned>(item.geometry->getTriangleCount());
    }
    const auto bound = first->getBound();
    first->setSubdivided(true);
    auto refined = collect(*first);
    unsigned refinedTriangles = 0;
    for (const auto& item : refined.getRenderList()){
        refinedTriangles += static_cast<unsigned>(item.geometry->getTriangleCount());
        for (const auto& vertex : item.geometry->getVertices()){
            check(std::isfinite(vertex.position.x) &&
                      std::abs(glm::length(vertex.normal) - 1) < 0.0001f,
                  "Refined meshes contain valid positions and normalized recomputed normals");
            check(std::isfinite(vertex.texCoord.x) && std::isfinite(vertex.texCoord.y),
                  "Texture seams retain finite interpolated coordinates");
        }
    }
    check(refinedTriangles == triangles * 4 && first->getSubdivided() && !second->getSubdivided(),
          "One subdivision level affects only the targeted model instance");
    auto other = collect(*second);
    check(other.getRenderList().front().geometry == before.front().geometry,
          "Other instances retain their shared original geometry");
    check(std::abs(first->getBound().getMinimum().y - bound.getMinimum().y) < 0.0001f,
          "Subdivision retains the model's support height");
    const auto cached = refined.getRenderList().front().geometry;
    first->setSubdivided(false);
    auto restored = collect(*first);
    check(restored.getRenderList().front().geometry == before.front().geometry,
          "Turning subdivision off restores the exact original mesh");
    first->setSubdivided(true);
    check(collect(*first).getRenderList().front().geometry == cached,
          "Turning subdivision on again reuses the in-memory mesh");
}

// Checks exact ray hits, nearest surfaces, nonuniform transforms and transparent walls.
void picking(){
    auto root = std::make_shared<vr::Group>();
    auto mesh = vr::GeometryFactory::createBox();
    auto model = vr::ModelInstance::create("box", mesh);
    auto transform = std::make_shared<vr::Transform>();
    transform->setMatrix(glm::scale(glm::translate(glm::mat4(1),{0, 0, 1}),{2, 0.5f, 3}));
    transform->addChild(model);
    root->addChild(transform);
    auto list = collect(*root);
    const auto hit = vr::pickModel(list.getRenderList(),{0, 0, 8},{0, 0, -1});
    check(hit && hit->instance == model.get() && std::abs(hit->distance - 5.5f) < 0.0001f,
          "Camera ray finds the nearest transformed surface at its world-space distance");
    check(!vr::pickModel(list.getRenderList(),{0, 2, 8},{0, 0, -1}), "Ray misses empty space");
    std::vector<glm::vec3> positions, normals;
    for (const auto& vertex : mesh->getVertices()){
        positions.push_back(vertex.position);
        normals.push_back(vertex.normal);
    }
    auto indices = mesh->getIndices();
    indices.erase(indices.begin(), indices.begin() + 6);
    mesh->setData(positions, indices, normals);
    const auto updated = vr::pickModel(collect(*root).getRenderList(),{0, 0, 8},{0, 0, -1});
    check(
        updated && std::abs(updated->distance - 8.5f) < 0.0001f,
        "Changing triangle data invalidates the picking BVH even when mesh bounds stay unchanged");
    auto wall = std::make_shared<vr::Transform>();
    wall->setMatrix(glm::translate(glm::mat4(1),{0, 0, 5}));
    auto state = std::make_shared<vr::State>();
    wall->setState(state);
    wall->addChild(vr::GeometryFactory::createBox());
    root->addChild(wall);
    check(!vr::pickModel(collect(*root).getRenderList(),{0, 0, 8},{0, 0, -1}),
          "An opaque foreground surface blocks selection behind it");
    state->setBlend(true);
    check(vr::pickModel(collect(*root).getRenderList(),{0, 0, 8},{0, 0, -1})->instance ==
              model.get(),
          "Transparent viewing walls allow selecting visible models behind them");
    auto glass = vr::ModelInstance::create("glass", vr::GeometryFactory::createSphere(16, 12));
    auto glassState = std::make_shared<vr::State>();
    glassState->setBlend(true);
    glassState->setUniform("uGlass", true);
    glass->setState(glassState);
    auto glassPlacement = std::make_shared<vr::Transform>();
    glassPlacement->setMatrix(glm::scale(glm::translate(glm::mat4(1),{0, 0, 6}), glm::vec3(0.4f)));
    glassPlacement->addChild(glass);
    root->addChild(glassPlacement);
    const auto glassHit = vr::pickModel(collect(*root).getRenderList(),{0, 0, 8},{0, 0, -1});
    check(glassHit && glassHit->instance == glass.get(),
          "Explicitly selectable refractive glass is targeted ahead of the model behind it");
}

// Checks filtered alpha sampling, see-through hits, subdivision UVs and deeper hits in one mesh.
void alphaPicking(){
    const std::vector<unsigned char> pixels{255, 255, 255, 0,   255, 255, 255, 255,
                                            255, 255, 255, 255, 255, 255, 255, 0};
    auto texture = std::make_shared<vr::Texture>(2, 2, pixels);
    check(texture->sampleAlpha({0.25f, 0.25f}) == 0 && texture->sampleAlpha({0.75f, 0.25f}) == 1 &&
              texture->sampleAlpha({0.25f, 0.75f}) == 1 &&
              texture->sampleAlpha({0.5f, 0.25f}) == 0.5f,
          "CPU alpha sampling preserves UV axes and bilinear edge filtering");
    check(texture->sampleAlpha({1.25f, 0.25f}) == 0 && texture->sampleAlpha({-0.25f, 0.25f}) == 1,
          "Repeated UVs wrap on both sides of the texture");
    vr::Texture clamped(2, 2, pixels, false);
    check(clamped.sampleAlpha({-2, 0.25f}) == 0 && clamped.sampleAlpha({2, 0.25f}) == 1,
          "Clamped alpha sampling retains edge texels");
    auto root = std::make_shared<vr::Group>();
    auto leaf = vr::ModelInstance::create("leaf", vr::GeometryFactory::createAlphaTestPanel());
    auto state = std::make_shared<vr::State>();
    state->setTexture(0, texture);
    state->setUniform("uAlphaCutoff", 0.5f);
    leaf->setState(state);
    root->addChild(leaf);
    auto background = vr::ModelInstance::create("background", vr::GeometryFactory::createBox());
    auto placement = std::make_shared<vr::Transform>();
    placement->setMatrix(glm::scale(glm::translate(glm::mat4(1),{0, 0.7f, -2}),{3, 3, 1}));
    placement->addChild(background);
    root->addChild(placement);
    for (const bool refined :{false, true}){
        leaf->setSubdivided(refined);
        const auto list = collect(*root);
        const auto hole = vr::pickModel(list.getRenderList(),{-0.5f, 0.7f, 3},{0, 0, -1});
        const auto solid = vr::pickModel(list.getRenderList(),{0.5f, 0.7f, 3},{0, 0, -1});
        check(hole && hole->instance == background.get() && solid && solid->instance == leaf.get(),
              "Transparent texels reveal background targets before and after subdivision");
    }
    leaf->setSubdivided(false);
    state->setUniform("uAlphaCutoff", 0.0f);
    check(vr::pickModel(collect(*root).getRenderList(),{-0.5f, 0.7f, 3},{0, 0, -1})->instance ==
              leaf.get(),
          "A material without alpha testing retains ordinary opaque picking");
    state->setUniform("uAlphaCutoff", 0.5f);
    auto layered = std::make_shared<vr::Geometry>();
    layered->setData({{-1, -1, 1},{1, -1, 1},{0, 1, 1},{-1, -1, 0},{1, -1, 0},{0, 1, 0}},
                     {0, 1, 2, 3, 4, 5}, std::vector<glm::vec3>(6,{0, 0, 1}),
                     {{0.25f, 0.25f},
                      {0.25f, 0.25f},
                      {0.25f, 0.25f},
                      {0.75f, 0.25f},
                      {0.75f, 0.25f},
                      {0.75f, 0.25f}});
    auto model = vr::ModelInstance::create("layered", layered);
    model->setState(state);
    const auto deeper = vr::pickModel(collect(*model).getRenderList(),{0, 0, 3},{0, 0, -1});
    check(deeper && std::abs(deeper->distance - 3) < 0.0001f,
          "Rejecting transparent triangles continues the BVH search inside the same mesh");
}

} // namespace

// Tests generated geometry entirely in memory without changing source assets.
int main(int argc, char** argv){
    try{
        if (argc != 2){
            return 1;
        }
        subdivision(vr::GeometryFactory::createBox());
        auto junction = std::make_shared<vr::Geometry>();
        junction->setData({{0, 0, 0},{1, 0, 0},{0.5f, 1, 0},{0.5f, -1, 0},{0.5f, 0, 1}},
                          {0, 1, 2, 1, 0, 3, 0, 1, 4},
                          std::vector<glm::vec3>(5, glm::vec3(0, 0, 1)));
        subdivision(junction);
        vr::Reader reader(argv[1]);
        subdivision(reader.readModel("models/monkey.obj"));
        picking();
        alphaPicking();
        std::cout << "Model interaction passed\n";
    } catch (const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
