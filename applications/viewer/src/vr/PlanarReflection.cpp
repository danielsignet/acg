#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/GeometryFactory.h>
#include <vr/PlanarReflection.h>

namespace vr{
// Reflects world coordinates through a normalized plane, including its translation.
glm::mat4 PlanarReflection::reflectionMatrix(const glm::vec4& plane){
    const float length = glm::length(glm::vec3(plane));
    if (!std::isfinite(length) || length < 1e-6f || !std::isfinite(plane.w)){
        throw std::invalid_argument("Invalid reflection plane");
    }
    const glm::vec4 p = plane / length;
    glm::mat4 reflection(1);
    for (unsigned column = 0; column < 4; ++column){
        for (unsigned row = 0; row < 3; ++row){
            reflection[column][row] -= 2 * p[row] * p[column];
        }
    }
    return reflection;
}

// Replaces reflection targets after viewport or sample-count changes.
void PlanarReflection::resize(int width, int height, AntialiasingMode mode){
    const int samples = mode == AntialiasingMode::Msaa ? 4 : 1;
    if (width == m_width && height == m_height && samples == m_samples){
        return;
    }
    m_target.reset();
    m_resolved.reset();
    m_width = width;
    m_height = height;
    m_samples = samples;
}

const Camera* PlanarReflection::plan(Scene& scene, const Camera& camera) const{
    const auto mirror = scene.findTransform("materialMirror");
    if (!m_enabled || !mirror || !mirror->getVisible() || m_width <= 0 || m_height <= 0)
        return nullptr;
    if (Frustum(camera.getProjection()*camera.getView()).classify(mirror->getBound()) == Visibility::Outside)
        return nullptr;
    const auto model = mirror->getMatrix();
    const auto normal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(model)))*glm::vec3(0,0,1));
    const glm::vec4 plane(normal,-glm::dot(normal,glm::vec3(model[3])));
    if (glm::dot(plane,glm::vec4(camera.getPosition(),1)) <= .01f) return nullptr;
    m_planned.setProjection(camera.getProjection());
    m_planned.setView(camera.getView()*reflectionMatrix(plane));
    return &m_planned;
}

// Updates a visible front-facing mirror; its own surface is excluded from the capture.
void PlanarReflection::render(Scene& scene, const Camera& camera, EnvironmentMap* environment,
                              ShadingMode shading, bool culling, bool portals){
    m_rendered = false;
    const auto mirror = scene.findTransform("materialMirror");
    if (!m_enabled || !mirror || !mirror->getVisible() || m_width <= 0 || m_height <= 0){
        return;
    }
    const auto model = mirror->getMatrix();
    const auto bound = mirror->getBound();
    if (Frustum(camera.getProjection() * camera.getView()).classify(bound) == Visibility::Outside){
        return;
    }
    const glm::vec3 normal =
        glm::normalize(glm::transpose(glm::inverse(glm::mat3(model))) * glm::vec3(0, 0, 1));
    const glm::vec4 plane(normal, -glm::dot(normal, glm::vec3(model[3])));
    if (glm::dot(plane, glm::vec4(camera.getPosition(), 1)) <= 0.01f){
        return;
    }
    if (!m_target){
        m_target = std::make_unique<ColorTarget>(
            m_width, m_height, Texture::depth(m_width, m_height, false, m_samples));
        if (m_samples > 1){
            m_resolved = std::make_unique<ColorTarget>(m_width, m_height);
        }
    }
    FramebufferState saved;
    m_target->bind();
    GLState::disable(GL_SCISSOR_TEST);
    GLState::disable(GL_BLEND);
    GLState::enable(GL_DEPTH_TEST);
    GLState::depthFunc(GL_LESS);
    GLState::depthMask(GL_TRUE);
    GLState::colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    GLState::disable(GL_SAMPLE_SHADING);
    if (m_samples > 1){
        GLState::enable(GL_MULTISAMPLE);
    } else{
        GLState::disable(GL_MULTISAMPLE);
    }
    glClearColor(0.004f, 0.006f, 0.01f, 1);
    GLState::clearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GLState::frontFace(GL_CW);
    Camera reflected;
    reflected.setProjection(camera.getProjection());
    reflected.setView(camera.getView() * reflectionMatrix(plane));
    auto visibility =
        portals ? scene.getPortalGraph().collect(reflected.getPosition(), nullptr) : PortalResult{};
    // A virtual eye behind a wall-mounted mirror can occupy an unrelated physical room.
    // In that case retain reflected-frustum and plane clipping without that room's portals.
    if (visibility.active &&
        visibility.origin != scene.getPortalGraph().collect(camera.getPosition(), nullptr).origin){
        visibility ={};
    }
    m_renderer.setCulling(culling);
    m_renderer.setShadingMode(shading);
    m_renderer.setEnvironment(environment);
    m_renderer.setReflectionPass(plane);
    m_renderer.render(*scene.getRoot(), reflected, nullptr, &visibility);
    if (m_resolved){
        m_target->resolveTo(*m_resolved);
    }
    m_rendered = true;
}

// Returns the reflected scene texture, or no texture when the mirror is not visible.
std::shared_ptr<Texture> PlanarReflection::getTexture() const{
    return !m_rendered ? nullptr : m_resolved ? m_resolved->getTexture() : m_target->getTexture();
}
} // namespace vr
