#pragma once
#include "Animation.h"
#include "Controls.h"
#include "DebugLines.h"
#include "Reader.h"
#include "SceneOverlay.h"
#include <GLFW/glfw3.h>

namespace vr{
class Application{
public:
    static constexpr const char* WindowTitle = "Scene graph viewer";

    // Creates the overlay and loads the initial demonstration scene.
    Application(GLFWwindow* window, std::filesystem::path root, std::filesystem::path scene,
                std::filesystem::path dotFile ={});

    // Processes input, callbacks and frame-rate statistics.
    void update(double time, double delta);

    // Draws the 3D scene and its overlay into the current framebuffer.
    void render();

    // Applies the command bound to a pressed key.
    void handleKey(int key);

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

    // Registers the procedural meshes referenced by the demonstration scene.
    static void registerGeometry(Reader& reader);

    // Restores the user's rendering toggles on a loaded scene.
    void applyRenderSettings(Scene& scene) const;

    // Attaches the light-ring animation while preserving its user settings.
    std::shared_ptr<OrbitCallback> createLightOrbit(Scene& scene) const;

    // Attaches the camera tour around the cow's world-space center.
    std::shared_ptr<LodDemoCallback> createLodDemo(Scene& scene) const;

    // Updates mouse look and hands camera-tour control back to the user.
    void updateCameraOrientation();

    // Moves the camera along its local axes using the current keyboard state.
    void moveCamera(float delta);

    // Moves the entire light ring using the arrow keys.
    void moveLightRing(float delta);

    // Returns the frame-scaled normal or fast movement distance.
    float movementSpeed(float delta) const;

    // Updates camera movement and interactive light-ring movement.
    void input(float delta);

    // Stops the camera tour while retaining its current position and direction.
    void stopLodDemo();

    // Draws the application HUD after rendering the 3D scene.
    void overlay();

    // Exports a debug graph and keeps the scene usable if writing fails.
    void exportGraph(const std::filesystem::path& path);
    GLFWwindow* m_window;
    std::filesystem::path m_root;
    std::filesystem::path m_sceneFile;
    std::filesystem::path m_dotFile;
    std::shared_ptr<Scene> m_scene;
    std::shared_ptr<Program> m_program;
    std::unique_ptr<RenderVisitor> m_renderer;
    std::unique_ptr<SceneOverlay> m_overlay;
    std::unique_ptr<DebugLines> m_debugLines;
    std::optional<glm::mat4> m_frozenFrustum;
    UpdateVisitor m_updater;
    std::shared_ptr<OrbitCallback> m_lightOrbitCallback;
    std::shared_ptr<LodDemoCallback> m_lodDemoCallback;
    std::array<bool, 4> m_pointLights{true, true, true, true};
    glm::vec3 m_position{0};
    glm::vec3 m_front{0, 0, -1};
    float m_yaw = -90;
    float m_pitch = -20;
    double m_mouseX = 0;
    double m_mouseY = 0;
    double m_frameTimeSeconds = 0;
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
    bool m_houseView = false;
    double m_framesPerSecond = 0;
    std::string m_status = "Ready";
};
} // namespace vr
