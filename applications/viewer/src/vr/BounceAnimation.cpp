#include <vr/BounceAnimation.h>
#include <vr/Geometry.h>
#include <vr/GraphVisitor.h>
#include <vr/State.h>
#include <cmath>
#include <stdexcept>

namespace vr{
glm::vec2 bounceParameters(const State& state){
    const auto& uniforms=state.getUniforms();
    const auto radius=uniforms.find("uBounceRadius"), time=uniforms.find("uBounceTime");
    return {radius==uniforms.end()?0:std::get<float>(radius->second),
            time==uniforms.end()?0:std::get<float>(time->second)};
}

glm::mat4 bounceMatrix(float radius, float time){
    if (radius<=0) return glm::mat4(1);
    const float height=std::abs(std::sin(2.4f*time));
    const float contact=height/.22f;
    const float sy=1-.3f*std::exp(-contact*contact)+.06f*height;
    const float sx=1/std::sqrt(sy);
    glm::mat4 result(1);
    result[0][0]=result[2][2]=sx; result[1][1]=sy;
    result[3][1]=radius*(sy-1+1.6f*height);
    return result;
}

BounceAnimation::BounceAnimation(float radius, double startTime, bool enabled)
    : m_radius(radius),m_time(startTime),m_enabled(enabled){
    if (!std::isfinite(radius) || radius<=0 || !std::isfinite(startTime) || startTime<0)
        throw std::invalid_argument("Bounce requires a positive radius and nonnegative finite time");
    m_time=std::fmod(m_time,2.617993877991494);
}

void BounceAnimation::update(Node& node, const UpdateContext& context){
    m_changed=m_enabled && context.animate && std::isfinite(context.delta) && context.delta>0;
    if (m_changed) m_time=std::fmod(m_time+context.delta,2.617993877991494);
    if (!node.getState()) node.setState(std::make_shared<State>());
    node.getState()->setUniform("uBounceRadius",m_radius);
    node.getState()->setUniform("uBounceTime",static_cast<float>(m_time));
    // The envelope keeps every graph level and light view culled safely.
    // Vertices and VBOs remain immutable, including subdivision variants.
    GraphVisitor padding;
    padding.setGeometryHandler([&](Geometry& mesh,const State&){
        mesh.setMotionPadding({m_radius*.25f,m_radius*2,m_radius*.25f});
    });
    node.accept(padding);
}
}
