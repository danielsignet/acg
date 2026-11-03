#include <vr/CallbackVisitor.h>
#include <vr/SkeletalAnimation.h>
#include <vr/SpinCallback.h>
#include <vr/BounceAnimation.h>
namespace vr{
void CallbackVisitor::apply(SkeletalAnimation& callback){apply(static_cast<UpdateCallback&>(callback));}
void CallbackVisitor::apply(JointAnimationCallback& callback){apply(static_cast<UpdateCallback&>(callback));}
void CallbackVisitor::apply(SpinCallback& callback){apply(static_cast<UpdateCallback&>(callback));}
void CallbackVisitor::apply(BounceAnimation& callback){apply(static_cast<UpdateCallback&>(callback));}
}
