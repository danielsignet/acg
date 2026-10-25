#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vr/Animation.h>
#include <vr/Controls.h>
#include <vr/DotVisitor.h>
#include <vr/Reader.h>

class SceneGraphTests final{
public:
    // Runs each independent feature check against the supplied resource root.
    void run(const std::filesystem::path& root){
        graphTests();
        lodTests();
        materialInheritanceTests();
        dotTests();
        animationTests();
        controlsTests();
        readerTests(root);
        std::cout << "Passed " << m_checks << " scene graph checks\n";
    }

private:
    unsigned m_checks = 0;

    // Counts one regression assertion and reports its failure.
    void check(bool value, const char* message){
        ++m_checks;
        if (!value){
            throw std::runtime_error(message);
        }
    }

    template <class F> void rejects(F f, const char* message){
        bool rejected = false;
        try{
            f();
        } catch (const std::exception&){
            rejected = true;
        }
        check(rejected, message);
    }

    // Compares vectors within the geometric floating-point tolerance.
    static bool near(const glm::vec3& a, const glm::vec3& b){
        return glm::length(a - b) < 0.0001f;
    }

    struct Probe : vr::PathVisitor{
        using PathVisitor::PathVisitor;

        struct Draw{
            const vr::Geometry* node;
            glm::mat4 model;
            vr::State state;
        };

        std::vector<Draw> draws;
        std::vector<glm::vec4> lights;

        // Handles a geometry instance with its resolved transform and rendering state.
        void geometry(vr::Geometry& g, const glm::mat4& m, const vr::State& s) override{
            draws.push_back({&g, m, s});
        }

        // Handles a light with the transform accumulated along its current path.
        void light(vr::Light& l, const glm::mat4& m, const vr::State&) override{
            lights.push_back(m * l.getPosition());
        }
    };

    // Checks dispatch, sharing, ownership, transforms and inherited state.
    void graphTests(){
        static_assert(std::is_abstract_v<vr::Node> && std::is_abstract_v<vr::NodeVisitor>);
        static_assert(std::is_base_of_v<vr::Group, vr::Transform> &&
                      std::is_base_of_v<vr::Node, vr::Camera> &&
                      std::is_base_of_v<vr::Node, vr::Light>);
        static_assert(!std::is_base_of_v<vr::Node, vr::State> &&
                      !std::is_base_of_v<vr::Node, vr::Material> &&
                      !std::is_base_of_v<vr::Node, vr::Texture>);
        auto root = std::make_shared<vr::Group>("root");
        auto a = std::make_shared<vr::Transform>("a");
        auto b = std::make_shared<vr::Transform>("b");
        auto g = vr::GeometryFactory::createGround();
        const auto vertex = g->getVertices().at(0).position;
        a->setMatrix(glm::translate(glm::mat4(1), glm::vec3(2, 0, 0)));
        b->setMatrix(glm::translate(glm::mat4(1), glm::vec3(-3, 0, 0)));
        a->addChild(g);
        b->addChild(g);
        root->addChild(a);
        root->addChild(b);
        auto parent = std::make_shared<vr::State>();
        parent->setLighting(true);
        parent->setCullFace(true);
        parent->setUniform("weight", 0.25f);
        root->setState(parent);
        auto local = std::make_shared<vr::State>();
        local->setLighting(false);
        local->setCullFace(false);
        local->setUniform("weight", 0.75f);
        a->setState(local);
        Probe p;
        root->accept(p);
        check(p.draws.size() == 2, "Shared geometry must be visited twice");
        check(p.draws[0].node == p.draws[1].node, "Instancing must reuse the same node");
        check(near(glm::vec3(p.draws[0].model[3]),{2, 0, 0}) &&
                  near(glm::vec3(p.draws[1].model[3]),{-3, 0, 0}),
              "Transform stack must restore between siblings");
        check(!p.draws[0].state.getLighting() && p.draws[1].state.getLighting(),
              "Lighting state must restore");
        check(!p.draws[0].state.getCullFace() && p.draws[1].state.getCullFace(),
              "Culling state must restore");
        check(std::get<float>(p.draws[0].state.getUniforms().at("weight")) == 0.75f &&
                  std::get<float>(p.draws[1].state.getUniforms().at("weight")) == 0.25f,
              "Uniform state must restore");
        check(near(g->getVertices().at(0).position, vertex),
              "Render traversal must preserve local geometry");
        check(g->getParents().size() == 2, "Shared node must retain both parents");
        rejects([&]{ a->addChild(root); }, "Cycle insertion must be rejected");
        rejects([&]{ root->addChild(root); }, "Self-cycle must be rejected");
        rejects([&]{ a->addChild(g); }, "Duplicate edge must be rejected");
        rejects([&]{ a->addChild(nullptr); }, "Null child must be rejected");
        unsigned updates = 0;
        g->setUpdateCallback(std::make_shared<vr::FunctionCallback>(
            [&](vr::Node&, const vr::UpdateContext&){ ++updates; }));
        vr::UpdateVisitor update;
        update.update(*root,{1, 0.01});
        check(updates == 1 && update.getCallbackCount() == 1,
              "A shared callback must execute once per update, not per path");
        update.update(*root,{2, 0.01});
        check(updates == 2, "Callback must execute again next frame");

        struct Dispatch : vr::NodeVisitor{
            unsigned groups = 0;
            unsigned transforms = 0;
            unsigned geometries = 0;

            // Processes this concrete node type through visitor dispatch.
            void apply(vr::Group& n) override{
                ++groups;
                traverse(n);
            }

            // Processes this concrete node type through visitor dispatch.
            void apply(vr::Transform& n) override{
                ++transforms;
                traverse(n);
            }

            // Processes this concrete node type through visitor dispatch.
            void apply(vr::Geometry&) override{
                ++geometries;
            }
        } dispatch;

        root->accept(dispatch);
        check(dispatch.groups == 1 && dispatch.transforms == 2 && dispatch.geometries == 2,
              "Concrete double dispatch");
        auto light = std::make_shared<vr::Light>();
        light->setPosition({1, 2, 3, 1});
        a->addChild(light);
        Probe lp;
        root->accept(lp);
        check(near(glm::vec3(lp.lights.at(0)),{3, 2, 3}), "Point light must inherit translation");
        light->setPosition({1, 2, 3, 0});
        Probe dp;
        root->accept(dp);
        check(near(glm::vec3(dp.lights.at(0)),{1, 2, 3}),
              "Directional light must ignore translation");
        auto texture =
            std::make_shared<vr::Texture>(1, 1, std::vector<unsigned char>{255, 255, 255, 255});
        vr::State textures, clear;
        textures.setTexture(0, texture);
        clear.setTexture(0, nullptr);
        check(textures.merged(clear).getTexture(0) == nullptr && textures.getTexture(0) == texture,
              "Explicit texture disable must override without mutating parent");
        textures.setLightEnabled(1, false);
        check(!textures.merged(vr::State()).getLightEnabled(1), "Per-light state inheritance");
        rejects([&]{ textures.setTexture(4, texture); }, "Invalid texture slot");
        rejects([&]{ textures.setLightEnabled(8, true); }, "Invalid light slot");
        rejects([&]{ g->setData({{0, 0, 0}},{0, 1, 2},{{0, 1, 0}}); },
                "Out-of-range geometry indices");
        rejects([&]{ g->setData({{0, 0, 0}},{0, 0, 0},{}); }, "Missing normals");

        struct ThrowOnceProbe final : Probe{
            bool shouldThrow = true;

            // Throws on the first draw to exercise state and matrix scope unwinding.
            void geometry(vr::Geometry& node, const glm::mat4& matrix,
                          const vr::State& state) override{
                if (shouldThrow){
                    shouldThrow = false;
                    throw std::runtime_error("Expected visitor callback failure");
                }
                Probe::geometry(node, matrix, state);
            }
        } throwingProbe;

        rejects([&]{ root->accept(throwingProbe); }, "Visitor callbacks must propagate errors");
        root->accept(throwingProbe);
        check(near(glm::vec3(throwingProbe.draws[0].model[3]),{2, 0, 0}) &&
                  near(glm::vec3(throwingProbe.draws[1].model[3]),{-3, 0, 0}),
              "Matrix scopes must restore after a callback throws");
        check(!throwingProbe.draws[0].state.getLighting() &&
                  throwingProbe.draws[1].state.getLighting(),
              "State scopes must restore after a callback throws");
    }

    // Checks distance boundaries and detail selection for each traversal path.
    void lodTests(){
        auto lod = std::make_shared<vr::LOD>("test");
        lod->addLevel(12, vr::GeometryFactory::createSphere(64, 32));
        lod->addLevel(24, vr::GeometryFactory::createSphere(20, 12));
        lod->addLevel(std::numeric_limits<float>::infinity(),
                      vr::GeometryFactory::createSphere(8, 6));
        check(lod->selectLevel(0) == 0 && lod->selectLevel(11.99f) == 0, "Near LOD");
        check(lod->selectLevel(12) == 1 && lod->selectLevel(23.99f) == 1, "Middle LOD boundary");
        check(lod->selectLevel(24) == 2 && lod->selectLevel(1000) == 2, "Far LOD boundary");
        auto transform = std::make_shared<vr::Transform>();
        transform->setMatrix(glm::translate(glm::mat4(1), glm::vec3(100, 0, 0)));
        transform->addChild(lod);
        Probe p;
        p.setEye({105, 0, 0});
        transform->accept(p);
        check(p.draws.size() == 1 && p.draws[0].node->getTriangleCount() == 3968,
              "LOD must use world-space distance of each instance");
        Probe far;
        far.setEye({0, 0, 0});
        transform->accept(far);
        check(far.draws.size() == 1 && far.draws[0].node->getTriangleCount() == 80,
              "LOD must select one coarse geometry");
        rejects(
            [&]{
                auto invalid = std::make_shared<vr::LOD>();
                invalid->addLevel(10, vr::GeometryFactory::createBox());
                invalid->addLevel(5, vr::GeometryFactory::createBox());
            },
            "Unsorted LOD ranges");
    }

    // Checks property-wise inheritance without changing shared source materials.
    void materialInheritanceTests(){
        auto root = std::make_shared<vr::Group>();
        auto parentMaterial = std::make_shared<vr::Material>();
        parentMaterial->setAmbient({0.8f, 0.7f, 0.6f});
        parentMaterial->setDiffuse({0.3f, 0.4f, 0.5f});
        parentMaterial->setSpecular({0.9f, 0.8f, 0.7f});
        parentMaterial->setShininess(90);
        auto parentState = std::make_shared<vr::State>();
        parentState->setMaterial(parentMaterial);
        root->setState(parentState);
        auto local = std::make_shared<vr::Group>();
        auto localState = std::make_shared<vr::State>();
        auto localMaterial = std::make_shared<vr::Material>();
        localMaterial->setDiffuse({0, 0, 1});
        localState->setMaterial(localMaterial);
        local->setState(localState);
        auto geometry = vr::GeometryFactory::createBox();
        local->addChild(geometry);
        root->addChild(local);
        root->addChild(geometry);
        Probe probe;
        root->accept(probe);
        const auto& merged = *probe.draws[0].state.getMaterial();
        check(near(merged.getAmbient(), parentMaterial->getAmbient()),
              "A diffuse-only child must inherit parent ambient");
        check(near(merged.getDiffuse(),{0, 0, 1}),
              "An explicit local diffuse must override the parent");
        check(near(merged.getSpecular(), parentMaterial->getSpecular()) &&
                  merged.getShininess() == 90,
              "Unset material properties must inherit");
        check(near(probe.draws[1].state.getMaterial()->getDiffuse(),{0.3f, 0.4f, 0.5f}),
              "Material overrides must not leak to siblings");
        check(near(parentMaterial->getDiffuse(),{0.3f, 0.4f, 0.5f}) &&
                  near(localMaterial->getAmbient(),{0.13f, 0.13f, 0.13f}),
              "Resolution must preserve source materials");
        localMaterial->setAmbient({0, 0, 0});
        localMaterial->setSpecular({0, 0, 0});
        localMaterial->setShininess(1);
        const auto explicitValues = parentMaterial->merged(*localMaterial);
        check(near(explicitValues.getAmbient(),{0, 0, 0}) &&
                  near(explicitValues.getSpecular(),{0, 0, 0}) &&
                  explicitValues.getShininess() == 1,
              "Explicit zero colors and minimum shininess must override");
        Probe changed;
        root->accept(changed);
        check(near(changed.draws[0].state.getMaterial()->getAmbient(),{0, 0, 0}),
              "Changes to a shared local material must be resolved on the next traversal");
    }

    // Checks graph identity, escaping, complete LOD export and output failures.
    void dotTests(){
        const auto occurrences = [](const std::string& text, const std::string& needle){
            std::size_t count = 0, position = 0;
            while ((position = text.find(needle, position)) != std::string::npos){
                ++count;
                position += needle.size();
            }
            return count;
        };
        auto root = std::make_shared<vr::Group>("root");
        auto a = std::make_shared<vr::Transform>("same");
        auto b = std::make_shared<vr::Transform>("same");
        auto shared = std::make_shared<vr::Group>("shared");
        auto geometry = vr::GeometryFactory::createBox();
        geometry->setName("mesh\"\\\nname");
        shared->addChild(geometry);
        a->addChild(shared);
        b->addChild(shared);
        auto lod = std::make_shared<vr::LOD>("detail");
        lod->addLevel(12, geometry);
        lod->addLevel(std::numeric_limits<float>::infinity(),
                      vr::GeometryFactory::createSphere(8, 6));
        root->addChild(a);
        root->addChild(b);
        root->addChild(lod);
        root->addChild(std::make_shared<vr::Light>("light"));
        root->addChild(std::make_shared<vr::Camera>("camera"));
        unsigned updates = 0;
        root->setState(std::make_shared<vr::State>());
        root->setUpdateCallback(std::make_shared<vr::FunctionCallback>(
            [&](vr::Node&, const vr::UpdateContext&){ ++updates; }));
        std::ostringstream output;
        vr::DotVisitor exporter(output);
        exporter.write(*root);
        const auto first = output.str();
        check(first.find("digraph SceneGraph {") == 0 && first.substr(first.size() - 2) == "}\n",
              "DOT must be a complete directed graph");
        check(occurrences(first, "fillcolor=") == 9 && occurrences(first, " -> ") == 10,
              "DOT must emit each unique node once and every edge");
        check(occurrences(first, "Transform\\nsame") == 2,
              "Equal names must not merge distinct objects");
        check(first.find("n1 -> n6;") != std::string::npos &&
                  first.find("n2 -> n6;") != std::string::npos,
              "Two parents must point to the same shared subtree");
        check(occurrences(first, "n6 -> n7;") == 1, "A shared subtree must only be expanded once");
        check(occurrences(first, "Geometry\\n") == 2 &&
                  first.find("[0, 12)") != std::string::npos &&
                  first.find("[12, inf)") != std::string::npos,
              "DOT must include inactive LOD levels and distance ranges");
        check(first.find("mesh\\\"\\\\\\nname") != std::string::npos,
              "DOT must escape quotes, backslashes and newlines in names");
        check(first.find("Light\\nlight") != std::string::npos &&
                  first.find("Camera\\ncamera") != std::string::npos,
              "DOT must dispatch light and camera nodes");
        check(first.find("[local state]") != std::string::npos &&
                  first.find("[update callback]") != std::string::npos && updates == 0,
              "DOT must describe state and callbacks without executing them");
        exporter.write(*root);
        check(output.str().substr(first.size()) == first,
              "Reusing DotVisitor must reset its node IDs and visited set");
        std::ostringstream broken;
        broken.setstate(std::ios::badbit);
        rejects([&]{ vr::DotVisitor(broken).write(*root); }, "DOT output errors must be reported");
    }

    // Checks imported hierarchies, shared meshes, textures and the demo scene.
    void readerTests(const std::filesystem::path& root){
        vr::Reader reader(root);
        auto model = reader.readModel("tests/data/hierarchy.dae");
        check(model == reader.readModel("tests/data/hierarchy.dae"),
              "Model cache must share loaded roots");
        Probe p;
        model->accept(p);
        check(p.draws.size() == 2, "Imported hierarchy has two mesh instances");
        check(p.draws[0].node == p.draws[1].node, "Assimp mesh instances must share geometry");
        check(near(glm::vec3(p.draws[0].model[3]),{2, 1, 0}) &&
                  near(glm::vec3(p.draws[1].model[3]),{2, 3, 0}),
              "Reader must preserve nested local transformations");
        reader.readModel("models/monkey.obj");
        auto crate = reader.readModel("models/texturedBox.obj");
        Probe cp;
        crate->accept(cp);
        check(!cp.draws.empty() && cp.draws[0].state.getTexture(0) != nullptr,
              "Imported diffuse texture must resolve inside the resource directory");
        rejects([&]{ reader.readModel("models/missing.obj"); },
                "Missing model must report an error");
        reader.registerNode("ground", vr::GeometryFactory::createGround());
        reader.registerNode("box", vr::GeometryFactory::createBox());
        reader.registerNode("torus", vr::GeometryFactory::createTorus(64, 24));
        reader.registerNode("alphatest-panel", vr::GeometryFactory::createAlphaTestPanel());
        reader.registerNode("sphere-low", vr::GeometryFactory::createSphere(8, 6));
        auto scene = reader.readScene("scenes/demo.xml");
        check(scene->findTransform("alphatest") != nullptr,
              "The alpha-test demonstration must be addressable by its feature name");
        Probe sp;
        sp.setEye(scene->getInitialEye());
        scene->getRoot()->accept(sp);
        check(sp.draws.size() == 18 && sp.lights.size() == 4,
              "Cow must replace the display sphere; four light markers remain");
        for (const auto& name :{"red", "green", "blue", "white"}){
            check(scene->findLight(name) && scene->findLight(name)->getPosition().w == 1,
                  "All four ring lights must be point lights");
        }
        const auto cow = scene->findTransform("cowDisplay");
        for (const auto& test : std::vector<std::pair<float, std::size_t>>{{6.0f, 9496u},
                                                                           {10.0f, 4796u},
                                                                           {14.0f, 2877u},
                                                                           {18.0f, 958u},
                                                                           {26.0f, 478u},
                                                                           {40.0f, 94u}}){
            Probe cowProbe;
            cowProbe.setEye({6, 2.02f, test.first});
            cow->accept(cowProbe);
            std::size_t triangles = 0;
            for (const auto& draw : cowProbe.draws){
                triangles += draw.node->getTriangleCount();
            }
            check(triangles == test.second,
                  "Cow LOD must select each of the six supplied OBJ geometries");
        }
        const auto imported = reader.getAsset("monkey");
        check(imported->getParents().size() == 3,
              "Three XML placements share one loaded model root");
        check(scene->findTransform("animated")->getUpdateCallback() != nullptr,
              "XML animation callback");
        const auto animated = scene->findTransform("animated");
        const auto base = animated->getMatrix();
        vr::UpdateVisitor updater;
        updater.update(*scene->getRoot(),{1, 1, true});
        const auto expected =
            base * glm::rotate(glm::mat4(1), glm::radians(38.0f), glm::vec3(0, 0, 1));
        check(near(glm::vec3(animated->getMatrix()[0]), glm::vec3(expected[0])) &&
                  near(glm::vec3(animated->getMatrix()[1]), glm::vec3(expected[1])) &&
                  near(glm::vec3(animated->getMatrix()[3]), glm::vec3(base[3])),
              "XML animation axis must produce a visible tumble while preserving base placement");
        vr::Camera camera;
        camera.setLookAt({1, 2, 3},{0, 0, 0});
        check(near(camera.getPosition(),{1, 2, 3}), "Camera eye from inverse view");
        rejects([&]{ camera.setPerspective(60, 0, 0.1f, 100); }, "Invalid projection");
    }

    // Checks callback motion, independent pause and camera-tour distances.
    void animationTests(){
        auto ring = std::make_shared<vr::Transform>("ring");
        ring->setMatrix(glm::translate(glm::mat4(1), glm::vec3(0, 5, 0)));
        auto marker = vr::GeometryFactory::createSphere(8, 6);
        for (const auto& offset :
             std::vector<glm::vec3>{{8, 0, 0},{0, 0, -8},{-8, 0, 0},{0, 0, 8}}){
            auto placement = std::make_shared<vr::Transform>();
            placement->setMatrix(glm::translate(glm::mat4(1), offset));
            auto light = std::make_shared<vr::Light>();
            light->setPosition({0, 0, 0, 1});
            placement->addChild(light);
            placement->addChild(marker);
            ring->addChild(placement);
        }
        auto orbit = std::make_shared<vr::OrbitCallback>(ring, 90.0f);
        ring->setUpdateCallback(orbit);
        vr::UpdateVisitor update;
        update.update(*ring,{1, 1, false});
        Probe moved;
        ring->accept(moved);
        check(near(glm::vec3(moved.lights.at(0)),{0, 5, -8}),
              "Parent orbit must rotate lights even when geometry animation is paused");
        bool aligned = true;
        for (std::size_t i = 0; i < 4; ++i){
            aligned =
                aligned && near(glm::vec3(moved.lights[i]), glm::vec3(moved.draws[i].model[3]));
        }
        check(aligned,
              "All light markers must follow their lights through the same parent transform");
        check(marker->getParents().size() == 4 && update.getCallbackCount() == 1,
              "One callback must animate a ring of four shared marker instances");
        orbit->setEnabled(false);
        update.update(*ring,{2, 1, true});
        Probe paused;
        ring->accept(paused);
        check(near(glm::vec3(paused.lights[0]), glm::vec3(moved.lights[0])),
              "Orbit pause must retain the ring orientation");
        orbit->moveBy({2, 0, -3});
        update.update(*ring,{3, 1, true});
        Probe shifted;
        ring->accept(shifted);
        bool translated = true;
        for (std::size_t i = 0; i < 4; ++i){
            translated =
                translated && near(glm::vec3(shifted.lights[i] - paused.lights[i]),{2, 0, -3});
        }
        check(translated, "Interactive translation must move all four paused lights together");
        auto target = std::make_shared<vr::Transform>();
        target->setMatrix(glm::translate(glm::mat4(1), glm::vec3(1, 2, 3)));
        auto spin = std::make_shared<vr::SpinCallback>(target, 90.0f, glm::vec3(0, 1, 0));
        target->setUpdateCallback(spin);
        update.update(*target,{1, 1, true});
        check(near(glm::vec3(target->getMatrix()[0]),{0, 0, -1}) &&
                  near(glm::vec3(target->getMatrix()[3]),{1, 2, 3}),
              "Spin must preserve base translation and rotate local axes");
        const auto before = target->getMatrix();
        update.update(*target,{2, 1, false});
        check(near(glm::vec3(before[0]), glm::vec3(target->getMatrix()[0])),
              "Geometry spin must honor its own pause control");
        auto camera = std::make_shared<vr::Camera>();
        const glm::vec3 center(6, 2.02f, 0);
        vr::LodDemoCallback demo(camera, center);
        demo.setEnabled(true);
        for (const auto& stage : std::vector<std::pair<double, float>>{{0.5, 40.0f},
                                                                       {2.0, 26.0f},
                                                                       {2.0, 18.0f},
                                                                       {2.0, 14.0f},
                                                                       {2.0, 10.0f},
                                                                       {2.0, 6.0f}}){
            demo.update(*camera,{0, stage.first, false});
            check(std::abs(glm::distance(camera->getPosition(), center) - stage.second) < 0.0001f,
                  "LOD tour must hold all six detail distances independently of geometry pause");
        }
        const auto demoEye = camera->getPosition();
        demo.setEnabled(false);
        demo.update(*camera,{0, 3, false});
        check(near(camera->getPosition(), demoEye),
              "Disabled LOD tour must leave the camera unchanged");
    }

    // Checks that each command has one unambiguous key binding.
    void controlsTests(){
        using Action = vr::Controls::Action;
        std::set<int> keys;
        std::set<Action> actions;
        bool unique = true;
        for (const auto& binding : vr::Controls::getBindings()){
            unique = unique && keys.insert(binding.key).second &&
                     actions.insert(binding.action).second &&
                     !vr::Controls::caption(binding.action).empty();
        }
        check(unique, "Control actions, keys and HUD captions must have one unambiguous binding");
        check(vr::Controls::find(GLFW_KEY_ESCAPE)->action == Action::Exit &&
                  vr::Controls::find(GLFW_KEY_R)->action == Action::ReloadScene &&
                  vr::Controls::find(GLFW_KEY_SPACE)->action == Action::ResetView,
              "Exit, reload and reset must retain their key bindings");
        check(vr::Controls::find(GLFW_KEY_F)->action == Action::FreezeFrustum,
              "F freezes the independent culling camera");
        check(vr::Controls::find(GLFW_KEY_C)->action == Action::Culling &&
                  vr::Controls::find(GLFW_KEY_Z)->action == Action::ShaderMenu,
              "The compact viewport exposes culling and the shading menu");
        bool points = true;
        for (unsigned i = 0; i < 5; ++i){
            const auto* binding = vr::Controls::find(GLFW_KEY_1 + static_cast<int>(i));
            points = points && binding && vr::Controls::pointLightIndex(binding->action) == i;
        }
        check(vr::Controls::find(GLFW_KEY_J)->action == Action::Shadows,
              "J toggles shadow mapping");
        check(points, "Keys 1-5 identify the four colored lights and daylight");
        check(vr::Controls::getBindings().size() == 12 && !vr::Controls::find(GLFW_KEY_P) &&
                  !vr::Controls::find(GLFW_KEY_V) && !vr::Controls::find(GLFW_KEY_F3),
              "Development actions do not add more viewport shortcuts");
        check(!vr::Controls::find(GLFW_KEY_UNKNOWN), "Unbound keys must not trigger an action");
    }
};

// Runs the regression suite and returns a failing exit status for any unmet requirement.
int main(int argc, char** argv){
    try{
        SceneGraphTests().run(argc > 1 ? argv[1] : ".");
        return 0;
    } catch (const std::exception& error){
        std::cerr << "Failed: " << error.what() << '\n';
        return 1;
    }
}
