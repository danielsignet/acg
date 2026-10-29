#pragma once
#include <vr/SceneRenderExtension.h>
#include <vr/RenderVisitor.h>
#include <vr/EnvironmentMap.h>
#include <vr/FramebufferState.h>
#include <vr/GLState.h>
#include <vr/FrameProfiler.h>
#include <algorithm>
#include <cmath>
#include <functional>

namespace pantheon{
// Destination subtrees remain hidden in the main scene. They are rendered into
// cropped HDR targets and projected onto the corresponding doorway surfaces.
class RoomPortalViews{
public:
    struct Portal{
        std::string room;
        std::shared_ptr<vr::Node> wing;
        std::shared_ptr<vr::Transform> surface;
        glm::vec3 minimum,maximum;
        std::unique_ptr<vr::ColorTarget> target;
        int width=0,height=0;
    };
    struct Stats{std::string room;int width,height;unsigned draws;};
    inline static std::vector<Stats> lastCaptures;

    explicit RoomPortalViews(const std::shared_ptr<vr::Group>& owner,std::vector<Portal> portals)
        : m_owner(owner),m_portals(std::move(portals)){}

    void setPrepareDestination(std::function<void(vr::Node&)> callback){
        m_prepareDestination=std::move(callback);
    }

    void render(const vr::SceneRenderContext& context,const std::string& active,
                const glm::vec3& sourceMin,const glm::vec3& sourceMax){
        if (m_capturing) return; // one level, no feedback or recursive capture
        lastCaptures.clear();
        for (auto& portal:m_portals) portal.surface->setVisible(false);
        if(context.environmentCapture) return; // no recursive live textures in reflection probes
        const auto owner=m_owner.lock();
        if (!owner || active.empty() || context.width<=0 || context.height<=0) return;
        const auto vp=context.camera.getProjection()*context.camera.getView();
        const auto source=project(vp,sourceMin,sourceMax);
        if (!source.valid) return;
        if (!m_renderer){
            m_renderer=std::make_unique<vr::RenderVisitor>(context.program);
            m_renderer->setEnvironmentCapture();
        }
        m_renderer->setEnvironment(context.environment);
        m_renderer->setShadingMode(context.shading);
        for (auto& portal:m_portals){
            if (portal.room==active) continue;
            const auto projected=project(vp,portal.minimum,portal.maximum);
            if (!projected.valid) continue;
            glm::vec2 low=glm::max(projected.low,source.low),high=glm::min(projected.high,source.high);
            low=glm::max(low,glm::vec2(-1));high=glm::min(high,glm::vec2(1));
            if (glm::any(glm::lessThanEqual(high-low,glm::vec2(.002f)))) continue;
            vr::FrameProfiler::Scope measured(context.profiler,vr::FrameProfiler::Stage::PortalViews);
            // A small pixel guard keeps filtering from sampling an uncaptured edge.
            low=glm::max(low-glm::vec2(4.0f/context.width,4.0f/context.height),glm::vec2(-1));
            high=glm::min(high+glm::vec2(4.0f/context.width,4.0f/context.height),glm::vec2(1));
            const int width=std::clamp(static_cast<int>(std::ceil((high.x-low.x)*.5f*context.width)),96,768);
            const int height=std::clamp(static_cast<int>(std::ceil((high.y-low.y)*.5f*context.height)),96,768);
            if (!portal.target || width!=portal.width || height!=portal.height){
                portal.target=std::make_unique<vr::ColorTarget>(width,height,vr::Texture::depth(width,height));
                portal.width=width;portal.height=height;
            }
            glm::mat4 crop(1);
            crop[0][0]=2/(high.x-low.x);crop[1][1]=2/(high.y-low.y);
            crop[3][0]=-(high.x+low.x)/(high.x-low.x);
            crop[3][1]=-(high.y+low.y)/(high.y-low.y);
            auto projection=context.camera.getProjection();
            // Keep distant tower tops in the texture, without changing main-view depth.
            const float nearPlane=projection[3][2]/(projection[2][2]-1);
            projection[2][2]=-(300+nearPlane)/(300-nearPlane);
            projection[3][2]=-600*nearPlane/(300-nearPlane);
            vr::Camera capture;
            capture.setView(context.camera.getView());capture.setProjection(crop*projection);
            {
                vr::FramebufferState saved;
                CaptureScope isolated(*owner,context.root,portal.wing.get(),m_capturing);
                if(m_prepareDestination) m_prepareDestination(*portal.wing);
                portal.target->bind();
                vr::GLState::disable(GL_SCISSOR_TEST);vr::GLState::disable(GL_BLEND);
                vr::GLState::enable(GL_DEPTH_TEST);vr::GLState::depthMask(GL_TRUE);
                vr::GLState::depthFunc(GL_LESS);
                vr::GLState::frontFace(glm::determinant(glm::mat3(capture.getView()))<0 ? GL_CW : GL_CCW);
                vr::GLState::colorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
                vr::GLState::disable(GL_MULTISAMPLE);vr::GLState::disable(GL_SAMPLE_SHADING);
                glClearColor(0,0,0,1);vr::GLState::clearDepth(1);
                glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                m_renderer->render(context.root,capture);
            }
            if (!portal.surface->getState()) portal.surface->setState(std::make_shared<vr::State>());
            portal.surface->getState()->setTexture(2,portal.target->getTexture());
            portal.surface->getState()->setUniform("uRoomPortalViewProjection",capture.getProjection()*capture.getView());
            portal.surface->setVisible(true);
            lastCaptures.push_back({portal.room,width,height,m_renderer->getStats().draws});
        }
    }

private:
    struct Rect{glm::vec2 low{1e20f},high{-1e20f};bool valid=false;};
    static Rect project(const glm::mat4& vp,const glm::vec3& low,const glm::vec3& high){
        if (vr::Frustum(vp).classify(vr::BoundingBox(low,high))==vr::Visibility::Outside) return {};
        Rect result;
        for(int corner=0;corner<8;++corner){
            const auto p=vp*glm::vec4(corner&1?high.x:low.x,corner&2?high.y:low.y,corner&4?high.z:low.z,1);
            if (p.w<=1e-4f) return {glm::vec2(-1),glm::vec2(1),true};
            const glm::vec2 ndc=glm::vec2(p)/p.w;
            result.low=glm::min(result.low,ndc);result.high=glm::max(result.high,ndc);
        }
        result.valid=true;return result;
    }
    class CaptureScope{
    public:
        CaptureScope(vr::Group& owner,vr::Node& root,vr::Node* destination,bool& capturing)
            : m_root(root),m_state(root.getState()),m_capturing(capturing){
            m_capturing=true;
            for(const auto& child:owner.getChildren()){
                m_visibility.emplace_back(child,child->getVisible());
                if (!child->getRoomId().empty()) child->setVisible(child.get()==destination);
                if (child->getName().compare(0,11,"roomPortal_")==0) child->setVisible(false);
            }
            // Frame uniforms are intentionally shared by the renderer's state
            // cache. Auxiliary views must not overwrite the main shadow switch.
        }
        ~CaptureScope(){
            m_root.setState(m_state);
            for(const auto& [node,visible]:m_visibility) node->setVisible(visible);
            m_capturing=false;
        }
    private:
        vr::Node& m_root;std::shared_ptr<vr::State> m_state;bool& m_capturing;
        std::vector<std::pair<std::shared_ptr<vr::Node>,bool>> m_visibility;
    };
    std::weak_ptr<vr::Group> m_owner;
    std::vector<Portal> m_portals;
    std::unique_ptr<vr::RenderVisitor> m_renderer;
    bool m_capturing=false;
    std::function<void(vr::Node&)> m_prepareDestination;
};
}
