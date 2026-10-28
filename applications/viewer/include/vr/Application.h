#pragma once
#include "Animation.h"
#include "Controls.h"
#include "DebugLines.h"
#include "DeferredRenderer.h"
#include "EnvironmentMap.h"
#include "Flashlight.h"
#include "FrameProfiler.h"
#include "ModelInstance.h"
#include "ModelPicker.h"
#include "PlanarReflection.h"
#include "Reader.h"
#include "ReflectionProbes.h"
#include "SceneOverlay.h"
#include "ShadowRenderer.h"
#include <GLFW/glfw3.h>

namespace vr{
class Application{
public:
    static constexpr const char* WindowTitle = "ACG";

    // Creates the overlay and loads the initial demonstration scene.
    Application(GLFWwindow* window, std::filesystem::path root, std::filesystem::path scene,
                std::filesystem::path dotFile ={});

    // Processes input, callbacks and frame-rate statistics.
    void update(double time, double delta);

    // Draws the 3D scene and its overlay into the current framebuffer.
    void render();

    // Applies the command bound to a pressed key.
    void handleKey(int key);

    // Dispatches a toolbar click expressed in framebuffer coordinates.
    void handleClick(float x, float y);

    // Updates the viewport and camera projection for a nonempty framebuffer.
    void resize(int width, int height);

    // Stops the camera tour and restores the scene's initial camera pose.
    void resetView();

    // Builds a replacement scene completely before releasing the current scene.
    void reload();

    // Runs the isolated framebuffer and scene-behavior regression checks.
    void validate();

    // Returns and clears the pending screenshot request.
    bool consumeScreenshotRequest();

private:
    friend class SceneOverlay;
    friend class RenderingValidator;
    friend class Viewer;

    // Executes the shared action used by keyboard, toolbar and validation paths.
    void execute(Controls::Action action);

    // Registers the procedural meshes referenced by the demonstration scene.
    static void registerGeometry(Reader& reader);

    // Restores the user's rendering toggles on a loaded scene.
    void applyRenderSettings(Scene& scene) const;

    // Reports whether the selected instance belongs to the car exhibit.
    bool carTargeted() const;

    // Keeps the car's beams and emissive headlamp surfaces on the same switch.
    void applyCarHeadlights(Scene& scene) const;

    // Attaches the light-ring animation while preserving its user settings.
    std::shared_ptr<OrbitCallback> createLightOrbit(Scene& scene) const;

    // Attaches the camera tour around the cow's world-space center.
    std::shared_ptr<LodDemoCallback> createLodDemo(Scene& scene) const;

    // Updates mouse look and hands camera-tour control back to the user.
    void updateCameraOrientation();

    // Moves the camera along its local axes using the current keyboard state.
    void moveCamera(float delta);

    // Moves the existing light ring along world axes using arrows and Page Up/Down.
    void moveLights(float delta);

    // Returns the frame-scaled normal or fast movement distance.
    float movementSpeed(float delta) const;

    // Updates camera orientation and movement from the current input state.
    void input(float delta);

    // Stops the camera tour while retaining its current position and direction.
    void stopLodDemo();

    // Temporarily pauses the light ring while preserving its user's animation preference.
    void freezeLightOrbit(bool frozen);

    // Draws the application HUD after rendering the 3D scene.
    void overlay();

    // Collects submission, culling and attachment counters for this scene frame.
    FrameProfiler::Counters performanceCounters() const;

    // Exports a debug graph and keeps the scene usable if writing fails.
    void exportGraph(const std::filesystem::path& path);

    // Changes wall appearance while retaining its solid portal boundaries.
    void applyWallAppearance(Scene& scene) const;

    // Starts the overhead inspection preset with visibility frozen inside room A.
    void showCullingDemo();

    // Synchronizes the camera pose and mouse-look angles for every view change.
    void lookAt(const glm::vec3& eye, const glm::vec3& target);

    // Updates portal visibility from the live or frozen eye independently of the observer.
    void updatePortalVisibility(const Camera& camera, const glm::mat4& viewProjection);

    // Draws doorway, eye and bounding-volume references from the observer camera.
    void drawVisibilityDebug(const Camera& camera, const glm::mat4& observerViewProjection);

    FrameProfiler m_profiler;
    GLFWwindow* m_window;
    std::filesystem::path m_root;
    std::filesystem::path m_sceneFile;
    std::filesystem::path m_dotFile;
    std::shared_ptr<Scene> m_scene;
    std::shared_ptr<Program> m_program;
    std::shared_ptr<GpuSkinning> m_gpuSkinning;
    bool m_gpuSkinningEnabled = true;
    bool m_shadowCaching = true, m_sceneCaching = true;
    std::unique_ptr<RenderVisitor> m_renderer;
    std::unique_ptr<DeferredRenderer> m_deferred;
    std::unique_ptr<EnvironmentMap> m_environment;
    std::unique_ptr<PlanarReflection> m_reflection;
    std::unique_ptr<ReflectionProbes> m_reflectionProbes;
    std::unique_ptr<ShadowRenderer> m_shadowRenderer;
    std::unique_ptr<Flashlight> m_flashlight;
    std::optional<ModelHit> m_target;
    std::unique_ptr<SceneOverlay> m_overlay;
    std::unique_ptr<DebugLines> m_debugLines;
    std::optional<glm::mat4> m_frozenFrustum;
    std::optional<glm::vec3> m_frozenEye;
    std::optional<bool> m_frozenLightOrbitEnabled;
    PortalResult m_portalResult;
    UpdateVisitor m_updater;
    std::shared_ptr<OrbitCallback> m_lightOrbitCallback;
    std::shared_ptr<LodDemoCallback> m_lodDemoCallback;
    std::array<bool, 5> m_pointLights{true, true, true, true, true};
    ShadingMode m_shadingMode = ShadingMode::Phong;
    bool m_shaderMenuOpen = false;
    BufferView m_bufferView = BufferView::Lit;
    AntialiasingMode m_antialiasingMode = AntialiasingMode::Off;
    ReflectionMode m_reflectionMode = ReflectionMode::Environment;
    bool m_bumpMappingEnabled = true;
    bool m_deferredEnabled = true, m_ambientOcclusionEnabled = true;
    bool m_bloomEnabled = false, m_depthOfFieldEnabled = false, m_bufferPreviews = true;
    glm::vec3 m_position{0};
    glm::vec3 m_front{0, 0, -1};
    float m_yaw = -90;
    float m_pitch = -20;
    double m_mouseX = 0;
    double m_mouseY = 0;
    double m_frameTimeSeconds = 0;
    double m_lastFrameTime = 0;
    unsigned m_frameCount = 0;
    int m_width = 1280;
    int m_height = 800;
    bool m_mouseLookActive = false;
    bool m_animationEnabled = true;
    bool m_lightingEnabled = true;
    bool m_multitextureEnabled = true;
    bool m_cullFaceEnabled = true;
    bool m_toonEnabled = true;
    bool m_screenshotRequested = false;
    bool m_frustumCulling = true;
    bool m_debugBounds = false;
    bool m_roofEnabled = true;
    bool m_showHud = true;
    bool m_showMenus = true;
    bool m_vsync = true;
    bool m_houseView = false;
    bool m_portalCulling = true;
    bool m_transparentWalls = true;
    bool m_shadowsEnabled = true;
    bool m_carHeadlightsEnabled = false;
    double m_framesPerSecond = 0;
    double m_portalCpuMs = 0;
    double m_lastSubdivisionMs = 0;
    std::string m_lastSubdivision;
    std::string m_status = "Ready";
};
} // namespace vr
