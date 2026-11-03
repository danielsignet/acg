#pragma once
namespace vr{
class UpdateCallback;
class SkeletalAnimation;
class JointAnimationCallback;
class SpinCallback;
class BounceAnimation;
class CallbackVisitor{
public:
    virtual ~CallbackVisitor()=default;
    virtual void apply(UpdateCallback&){}
    virtual void apply(SkeletalAnimation&);
    virtual void apply(JointAnimationCallback&);
    virtual void apply(SpinCallback&);
    virtual void apply(BounceAnimation&);
};
}
