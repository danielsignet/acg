#include <vr/GraphVisitor.h>
#include <vr/Transform.h>
#include <vr/Camera.h>
#include <vr/Light.h>
namespace vr{
void GraphVisitor::apply(Transform& node){if(onTransform)onTransform(node);apply(static_cast<Group&>(node));}
void GraphVisitor::apply(Light& node){if(onNode)onNode(node);}
void GraphVisitor::apply(Camera& node){if(onNode)onNode(node);}
}
