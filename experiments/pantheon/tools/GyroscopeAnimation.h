#pragma once
#include <vr/Reader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

// This entire demonstration belongs to the removable Pantheon experiment.
namespace pantheon{
constexpr double Cycle = 20.0;
enum class RotationMethod{ Euler, AxisAngle, Quaternion };
enum class Joint{ Heading, Pitch, Roll };

inline double phase(double time){
    const double result = std::fmod(time, Cycle);
    return result < 0 ? result + Cycle : result;
}

// x = heading about Y, y = pitch about X, z = roll about Z.
// Equal heading/roll input cancels at pitch=90, for a genuine rank-two
// Y-X-Z Euler parameterization. The motors keep running during this phase.
inline glm::dvec3 eulerAngles(double time){
    const double t = phase(time);
    if (t < 4) return glm::radians(glm::dvec3(0, 20 + 70*t/4, 0));
    if (t < 12) return glm::radians(glm::dvec3((t-4)*22.5, 90, (t-4)*22.5));
    if (t < 16){
        const double u = (t-12)/4;
        return glm::radians(glm::dvec3(180+180*u, 90-70*u, 180+180*u));
    }
    return glm::radians(glm::dvec3(360, 20, 360));
}

inline glm::dmat4 eulerMatrix(const glm::dvec3& angles){
    auto m = glm::rotate(glm::dmat4(1), angles.x, glm::dvec3(0,1,0));
    m = glm::rotate(m, angles.y, glm::dvec3(1,0,0));
    return glm::rotate(m, angles.z, glm::dvec3(0,0,1));
}

// Unit quaternion key poses authored directly as axis-angle rotations. No
// Euler conversion or component-wise Euler interpolation drives this track.
inline glm::dquat quaternionPose(double time){
    static const std::array<glm::dquat,6> keys{{
        glm::angleAxis(glm::radians(20.0), glm::dvec3(1,0,0)),
        // This smooth segment crosses pitch=90 halfway through, rather than
        // changing the motion's tangent at the pole of the display mechanism.
        glm::angleAxis(glm::radians(160.0), glm::dvec3(1,0,0)),
        glm::angleAxis(glm::radians(135.0), glm::normalize(glm::dvec3(.3,1,.4))),
        glm::angleAxis(glm::radians(170.0), glm::normalize(glm::dvec3(.2,.3,1))),
        glm::angleAxis(glm::radians(240.0), glm::normalize(glm::dvec3(.6,-.4,1))),
        glm::angleAxis(glm::radians(20.0), glm::dvec3(1,0,0))
    }};
    const double step = phase(time)/4.0;
    const auto index = static_cast<std::size_t>(step);
    auto end = keys[index+1];
    // q and -q describe the same orientation. Take the short arc.
    if (glm::dot(keys[index], end) < 0) end = -end;
    return glm::normalize(glm::slerp(keys[index], end, step-index));
}

inline glm::dmat4 orientation(RotationMethod method, double time){
    if (method == RotationMethod::Euler) return eulerMatrix(eulerAngles(time));
    if (method == RotationMethod::AxisAngle){
        return glm::rotate(glm::dmat4(1), phase(time)*glm::two_pi<double>()/Cycle,
                           glm::normalize(glm::dvec3(.45,1,.35)));
    }
    return glm::mat4_cast(quaternionPose(time));
}

inline double unwrap(double value, double previous){
    return previous + std::remainder(value-previous, glm::two_pi<double>());
}

// Inverse kinematics only: arrange the mechanical display's three hinge joints
// around the already-computed orientation. The quaternion track never reads
// these angles back. Select the closest Euler branch so a physical ring does
// not jump by 180 degrees when the desired orientation passes a pole.
inline glm::dvec3 displayJoints(const glm::dmat4& m, const glm::dvec3& previous){
    const double sine = std::clamp(-m[2][1], -1.0, 1.0);
    const double pitch = std::asin(sine);
    if (std::abs(std::cos(pitch)) < 1e-7){
        const double h = previous.x;
        const double difference = std::atan2(m[1][0], m[0][0]);
        return {h, unwrap(pitch, previous.y),
                unwrap(sine > 0 ? h-difference : -difference-h, previous.z)};
    }
    glm::dvec3 a(std::atan2(m[2][0],m[2][2]), pitch,
                 std::atan2(m[0][1],m[1][1]));
    glm::dvec3 b(a.x+glm::pi<double>(),
                 pitch >= 0 ? glm::pi<double>()-pitch : -glm::pi<double>()-pitch,
                 a.z+glm::pi<double>());
    for (int i=0; i<3; ++i){
        a[i] = unwrap(a[i],previous[i]);
        b[i] = unwrap(b[i],previous[i]);
    }
    const auto da=a-previous, db=b-previous;
    return glm::dot(da,da) <= glm::dot(db,db) ? a : b;
}

class GyroscopeAnimation final : public vr::UpdateCallback{
public:
    GyroscopeAnimation(const std::shared_ptr<vr::Transform>& target, RotationMethod method,
                       Joint joint, double initialTime=0)
        : m_target(target), m_base(target ? target->getMatrix() : glm::mat4(1)), m_method(method), m_joint(joint),
          m_time(initialTime){
        if (!target || !std::isfinite(initialTime)) throw std::invalid_argument("Invalid gyroscope target/time");
    }

    void update(vr::Node& node, const vr::UpdateContext& context) override{
        if (auto target=m_target.lock(); target && target.get()==&node){
            if (context.animate && std::isfinite(context.delta) && context.delta>=0){
                // Bound the clock to keep sub-frame precision in long sessions.
                m_time=phase(m_time+context.delta);
            }
            m_display = m_method==RotationMethod::Euler ? eulerAngles(m_time)
                : displayJoints(orientation(m_method,m_time),m_display);
            const int index=static_cast<int>(m_joint);
            static const std::array<glm::vec3,3> axes{{{0,1,0},{1,0,0},{0,0,1}}};
            target->setMatrix(m_base * glm::rotate(glm::mat4(1),
                              static_cast<float>(m_display[index]), axes[index]));
        }
    }
private:
    std::weak_ptr<vr::Transform> m_target;
    glm::mat4 m_base;
    RotationMethod m_method;
    Joint m_joint;
    double m_time;
    glm::dvec3 m_display{0};
};

inline void registerGyroscopes(){
    vr::Reader::registerTransformAnimation("gyroscope",[](const auto& target, const auto& attrs){
        const auto& method=attrs.at("rotation-method");
        const auto& joint=attrs.at("joint");
        if (method!="euler" && method!="axis-angle" && method!="quaternion")
            throw std::invalid_argument("Invalid gyroscope rotation method");
        if (joint!="heading" && joint!="pitch" && joint!="roll")
            throw std::invalid_argument("Invalid gyroscope joint");
        const auto time=attrs.find("demo-time");
        double start=0;
        if (time!=attrs.end()){
            std::size_t end=0;
            start=std::stod(time->second,&end);
            if (end!=time->second.size()) throw std::invalid_argument("Invalid demo-time");
        }
        return std::make_shared<GyroscopeAnimation>(target,
            method=="euler" ? RotationMethod::Euler : method=="axis-angle" ? RotationMethod::AxisAngle : RotationMethod::Quaternion,
            joint=="heading" ? Joint::Heading : joint=="pitch" ? Joint::Pitch : Joint::Roll,start);
    });
}
} // namespace pantheon
