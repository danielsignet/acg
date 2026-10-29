#include "GyroscopeAnimation.h"
#include <vr/UpdateVisitor.h>
#include <fstream>
#include <iostream>

namespace{
unsigned checks=0;
void check(bool pass,const char* message){
    ++checks;
    if (!pass) throw std::runtime_error(message);
}
template<class F> void rejects(F action,const char* message){
    bool rejected=false;
    try{ action(); } catch(const std::exception&){ rejected=true; }
    check(rejected,message);
}
double error(const glm::dmat4& a,const glm::dmat4& b){
    double result=0;
    for(int c=0;c<4;++c) for(int r=0;r<4;++r)
        result=std::max(result,std::abs(a[c][r]-b[c][r]));
    return result;
}
glm::dvec3 angularChange(const glm::dmat4& before,const glm::dmat4& after,double step){
    const glm::dmat3 delta=glm::dmat3(after)*glm::transpose(glm::dmat3(before));
    return glm::dvec3(delta[1][2]-delta[2][1],delta[2][0]-delta[0][2],
                      delta[0][1]-delta[1][0])/(2*step);
}
void singularityTests(){
    constexpr double eps=1e-5;
    const glm::dvec3 locked(0,glm::half_pi<double>(),0);
    const auto before=pantheon::eulerMatrix(locked);
    glm::dmat3 eulerJacobian(0),quaternionJacobian(0);
    const auto q=glm::angleAxis(glm::half_pi<double>(),glm::dvec3(1,0,0));
    for(int i=0;i<3;++i){
        auto changed=locked;changed[i]+=eps;
        eulerJacobian[i]=angularChange(before,pantheon::eulerMatrix(changed),eps);
        glm::dvec3 axis(0);axis[i]=1;
        quaternionJacobian[i]=angularChange(glm::mat4_cast(q),
            glm::mat4_cast(glm::normalize(q*glm::angleAxis(eps,axis))),eps);
    }
    check(std::abs(glm::determinant(eulerJacobian))<1e-9,
          "Euler Jacobian must lose rank at middle-axis 90 degrees");
    check(glm::length(eulerJacobian[0]+eulerJacobian[2])<1e-9,
          "Euler heading and roll axes must be antiparallel at lock");
    check(std::abs(glm::determinant(quaternionJacobian)-1)<1e-8,
          "Quaternion local controls must retain three independent rotation axes at 90 degrees");
    check(error(pantheon::orientation(pantheon::RotationMethod::Euler,5),
                pantheon::orientation(pantheon::RotationMethod::Euler,11))<1e-12,
          "Equal Euler heading/roll input must cancel throughout the lock demonstration");
    check(error(pantheon::orientation(pantheon::RotationMethod::Quaternion,2),
                pantheon::orientation(pantheon::RotationMethod::Quaternion,2.1))>.01,
          "Quaternion orientation must continue moving after passing 90 degrees");
    std::cout << "Euler control determinant: " << glm::determinant(eulerJacobian)
              << "; quaternion control determinant: " << glm::determinant(quaternionJacobian) << '\n';
    glm::dvec3 previous(0);
    for(int i=0;i<=400;++i){
        const auto angles=pantheon::displayJoints(
            pantheon::orientation(pantheon::RotationMethod::Quaternion,i*.01),previous);
        check(glm::length(angles-previous)<.36,
              "Display bearings must pass through 90 degrees without a branch jump");
        previous=angles;
    }
}

void trackAndRigTests(){
    for(auto method:{pantheon::RotationMethod::Euler,pantheon::RotationMethod::AxisAngle,
                     pantheon::RotationMethod::Quaternion}){
        auto root=std::make_shared<vr::Group>("rig");
        auto heading=std::make_shared<vr::Transform>("heading");
        auto pitch=std::make_shared<vr::Transform>("pitch");
        auto roll=std::make_shared<vr::Transform>("roll");
        root->addChild(heading);heading->addChild(pitch);pitch->addChild(roll);
        const std::array<std::shared_ptr<vr::Transform>,3> nodes{{heading,pitch,roll}};
        for(int j=0;j<3;++j) nodes[j]->setUpdateCallback(
            std::make_shared<pantheon::GyroscopeAnimation>(nodes[j],method,static_cast<pantheon::Joint>(j)));
        vr::UpdateVisitor update;
        double time=0;
        for(int frame=0;frame<3600;++frame){
            const double delta=(frame%29==0) ? .11 : 1.0/120;
            time+=delta;
            update.update(*root,{time,delta,true});
            glm::dmat4 h(heading->getMatrix()),p(pitch->getMatrix()),r(roll->getMatrix());
            check(error(h*p*r,pantheon::orientation(method,time))<2e-6,
                  "Connected hinge hierarchy must reconstruct the independent orientation track");
            // Parent and child bearing centres must remain exactly coincident
            // at both ends of every joint for every sampled pose.
            const glm::dvec4 y(0,2.65,0,1),x(2.4,0,0,1),z(0,0,1.95,1);
            check(glm::length(h*y-y)<1e-6 && glm::length(h*p*x-h*x)<1e-6 &&
                  glm::length(h*p*r*z-h*p*z)<1e-6,
                  "A moving ring's trunnions must remain in its parent's bearings");
        }
        const auto paused=roll->getMatrix();
        update.update(*root,{time+100,100,false});
        check(error(glm::dmat4(paused),glm::dmat4(roll->getMatrix()))<1e-7,
              "Pause must stop all three gyroscope methods");
        check(error(pantheon::orientation(method,0),pantheon::orientation(method,20))<1e-12,
              "Rotation loop must close without an orientation jump");
    }
    for(int i=0;i<10000;++i){
        const double time=i*7381.127;
        const auto q=pantheon::quaternionPose(time);
        const glm::dmat3 r(pantheon::orientation(pantheon::RotationMethod::Quaternion,time));
        check(std::abs(glm::length(q)-1)<1e-12 && std::abs(glm::determinant(r)-1)<1e-12,
              "Long-running quaternion samples must stay normalized and rigid");
    }
    // Every SLERP segment uses constant angular increments, including negative
    // quaternion dot products and the closing segment.
    for(int segment=0;segment<5;++segment){
        double reference=-1;
        for(int i=0;i<20;++i){
            const auto a=pantheon::quaternionPose(segment*4+i*.2);
            const auto b=pantheon::quaternionPose(segment*4+(i+1)*.2);
            const double angle=2*std::acos(std::clamp(std::abs(glm::dot(a,b)),0.0,1.0));
            if(reference<0) reference=angle;
            check(std::abs(angle-reference)<1e-10,"SLERP must preserve angular speed within a key segment");
        }
    }
    auto target=std::make_shared<vr::Transform>();
    const auto base=glm::scale(glm::translate(glm::mat4(1),glm::vec3(1,2,3)),glm::vec3(2));
    target->setMatrix(base);
    pantheon::GyroscopeAnimation callback(target,pantheon::RotationMethod::Quaternion,pantheon::Joint::Heading);
    callback.update(*target,{1,1,true});
    check(glm::vec3(target->getMatrix()[3])==glm::vec3(1,2,3) &&
          std::abs(glm::length(glm::vec3(target->getMatrix()[0]))-2)<1e-6,
          "Callbacks must preserve the base translation and scale");
}

void readerTests(const std::filesystem::path& root){
    pantheon::registerGyroscopes();
    rejects([]{pantheon::registerGyroscopes();},"Duplicate animation registration must fail");
    rejects([]{vr::Reader::registerTransformAnimation("spin",{});},"Built-in spin must remain reserved");
    const auto file=root/"experiments/pantheon/output/gyro-reader-test.xml";
    std::filesystem::create_directories(file.parent_path());
    {
        std::ofstream out(file);
        out << "<scene><transform name=\"probe\" translate=\"1 2 3\" animation=\"gyroscope\" "
               "rotation-method=\"quaternion\" joint=\"pitch\" demo-time=\"2\"/></scene>";
    }
    vr::Reader reader(root);
    for(int reload=0;reload<2;++reload){
        auto scene=reader.readScene(file);
        vr::UpdateVisitor update;
        update.update(*scene->getRoot(),{0,0,true});
        const auto matrix=scene->findTransform("probe")->getMatrix();
        check(glm::length(glm::vec3(matrix[1])-glm::vec3(0,0,1))<1e-6,
              "XML factory must install and restore quaternion animations on scene reload");
        check(glm::vec3(matrix[3])==glm::vec3(1,2,3),"Factory must preserve placement");
    }
    auto target=std::make_shared<vr::Transform>();
    rejects([&]{vr::Reader::createTransformAnimation("missing",target,{});},"Unknown animation must fail visibly");
    rejects([&]{vr::Reader::createTransformAnimation("gyroscope",target,
        {{"rotation-method","euler"},{"joint","invalid"}});},"Invalid joint must be rejected");
    rejects([&]{vr::Reader::createTransformAnimation("gyroscope",target,
        {{"rotation-method","invalid"},{"joint","pitch"}});},"Invalid method must be rejected");
    rejects([&]{vr::Reader::createTransformAnimation("gyroscope",target,
        {{"rotation-method","euler"},{"joint","pitch"},{"demo-time","nan"}});},"Nonfinite clock must be rejected");
}
}

int main(int argc,char** argv){
    try{
        if(argc!=2) throw std::runtime_error("gyroscope_tests resource-root");
        singularityTests();trackAndRigTests();readerTests(argv[1]);
        std::cout << "Passed " << checks << " gyroscope checks\n";
        return 0;
    } catch(const std::exception& error){
        std::cerr << error.what() << '\n';return 1;
    }
}
