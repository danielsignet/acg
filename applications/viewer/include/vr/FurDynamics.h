#pragma once
#include "Transform.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr{
// One bounded mass/spring per articulated patch, not one CPU particle per hair.
// The fixed simulation step makes damping independent of the render frame rate.
class FurSpring{
public:
    void step(const glm::vec3& force,float dt){
        constexpr float omega=17.0f,damping=.58f,limit=.12f;
        velocity+=(omega*omega*(force-displacement)-2*damping*omega*velocity)*dt;
        displacement+=velocity*dt;
        const auto length=glm::length(displacement);
        if(length>limit){
            const auto normal=displacement/length;
            displacement=normal*limit;
            velocity-=normal*std::max(glm::dot(velocity,normal),0.0f);
        }
    }
    glm::vec3 displacement{0},velocity{0};
};

inline glm::mat4 furDancePose(int part,double time){
    const float t=static_cast<float>(time),beat=3.8f*t;
    if(part==0){
        auto result=glm::translate(glm::mat4(1),glm::vec3(.12f*std::sin(beat),.12f*(1-std::cos(2*beat)),0));
        result=glm::rotate(result,.20f*std::sin(beat*.5f),glm::vec3(0,1,0));
        return glm::rotate(result,.11f*std::sin(beat),glm::vec3(0,0,1));
    }
    const float side=(part==1 || part==3)?-1.0f:1.0f;
    if(part<=2){
        auto result=glm::translate(glm::mat4(1),glm::vec3(side*.79f,1.68f,0));
        result=glm::rotate(result,side*(.45f+.55f*std::sin(beat+side*.7f)),glm::vec3(0,0,1));
        return glm::rotate(result,.42f*std::sin(beat+side),glm::vec3(1,0,0));
    }
    return glm::translate(glm::mat4(1),glm::vec3(side*.46f,.22f+.18f*std::max(0.0f,std::sin(beat+side*1.57f)),.12f)) *
        glm::rotate(glm::mat4(1),.16f*std::sin(beat+side*1.57f),glm::vec3(1,0,0));
}

class FurDanceAnimation final:public UpdateCallback{
public:
    FurDanceAnimation(const std::shared_ptr<Transform>& target,int part,double phase=0)
        :m_target(target),m_base(target->getMatrix()),m_part(part),m_time(phase){
        if(part<0 || part>4 || !std::isfinite(phase))throw std::invalid_argument("Invalid fur dance part/phase");
        if(!target->getState())target->setState(std::make_shared<State>());
        target->setMatrix(m_base*furDancePose(m_part,m_time));
    }
    void update(Node& node,const UpdateContext& context) override{
        m_changed=false;
        const auto target=m_target.lock();
        if(!target || target.get()!=&node || !context.animate || !std::isfinite(context.delta) || context.delta<=0)return;
        // Bound work after a stall; carry sub-step time across ordinary frames.
        m_remainder+=std::min(context.delta,.1);
        constexpr double step=1.0/240;
        while(m_remainder+1e-10>=step){
            m_time+=step;m_remainder-=step;
            constexpr double h=.002;
            const auto center=point(m_time);
            const auto acceleration=(point(m_time+h)-2.0f*center+point(m_time-h))/static_cast<float>(h*h);
            auto rotation=m_part>=3?glm::mat4(1):furDancePose(0,m_time);
            if(m_part)rotation*=furDancePose(m_part,m_time);
            const glm::vec3 wind(.028f*std::sin(static_cast<float>(m_time)*1.9f),-.025f,.018f*std::cos(static_cast<float>(m_time)*1.3f));
            auto force=glm::transpose(glm::mat3(rotation))*(wind-acceleration*.009f);
            const auto length=glm::length(force);
            if(length>.11f)force*=.11f/length;
            m_spring.step(force,static_cast<float>(step));
        }
        target->setMatrix(m_base*furDancePose(m_part,m_time));
        target->getState()->setUniform("uFurMotion",m_spring.displacement);
        m_changed=true;
    }
    bool changesRenderedContent()const override{return m_changed;}
    const FurSpring& spring()const{return m_spring;}
    double time()const{return m_time;}
private:
    glm::vec3 point(double time)const{
        auto matrix=m_part>=3?glm::mat4(1):furDancePose(0,time);
        if(m_part)matrix*=furDancePose(m_part,time);
        const glm::vec3 tip=m_part==0?glm::vec3(0,1.8f,.3f):m_part<=2?glm::vec3(0,-.7f,0):glm::vec3(0,0,.25f);
        return glm::vec3(matrix*glm::vec4(tip,1));
    }
    std::weak_ptr<Transform> m_target;
    glm::mat4 m_base;
    int m_part;
    double m_time,m_remainder=0;
    FurSpring m_spring;
    bool m_changed=false;
};
}
