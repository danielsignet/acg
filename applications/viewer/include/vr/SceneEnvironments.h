#pragma once
#include "BoundingBox.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace vr{
struct RoomSky{
    std::string id;
    std::filesystem::path image;
    // Zero uses the default mean-luminance normalization; positive is an
    // explicit multiplier, useful for a night sky that should stay dark.
    float exposure = 0;
};

struct SkyRegion{
    std::string room;
    unsigned sky = 0;
    BoundingBox bounds;
    // Optional vertical cylinder inside the bounds: xyz centre, w radius.
    glm::vec4 cylinder{0};
    BoundingBox opening;
    glm::vec3 outward{0};

    bool contains(const glm::vec3& point) const{
        constexpr float epsilon=1e-4f;
        if (glm::any(glm::lessThan(point,bounds.getMinimum()-epsilon)) ||
            glm::any(glm::greaterThan(point,bounds.getMaximum()+epsilon))) return false;
        const glm::vec2 offset(point.x-cylinder.x,point.z-cylinder.z);
        return cylinder.w==0 || glm::dot(offset,offset)<=cylinder.w*cylinder.w+epsilon;
    }

    // Tests the actual opening, independent of the observer's room. In
    // particular, an outgoing ray can sample the world's sky through a door.
    bool crossesOpening(const glm::vec3& origin,const glm::vec3& direction,float& distance) const{
        const float denominator=glm::dot(direction,outward);
        if (std::abs(denominator)<1e-6f) return false;
        distance=glm::dot(opening.getMinimum()-origin,outward)/denominator;
        if (distance < -1e-4f) return false;
        const auto hit=origin+std::max(distance,0.0f)*direction;
        return glm::all(glm::greaterThanEqual(hit,opening.getMinimum()-1e-4f)) &&
               glm::all(glm::lessThanEqual(hit,opening.getMaximum()+1e-4f));
    }
};

class SceneEnvironments{
public:
    static constexpr unsigned MaxSkies=4,MaxRegions=4;

    void build(std::vector<RoomSky> skies,std::vector<SkyRegion> regions){
        if (skies.size()+1>MaxSkies || regions.size()>MaxRegions)
            throw std::runtime_error("Too many scene environments");
        std::set<std::string> ids,rooms;
        for (const auto& sky:skies){
            if (sky.id.empty() || sky.image.empty() || !std::isfinite(sky.exposure) ||
                sky.exposure<0 || !ids.insert(sky.id).second)
                throw std::runtime_error("Invalid or duplicate room sky");
        }
        for (auto& region:regions){
            const auto lo=region.bounds.getMinimum(),hi=region.bounds.getMaximum();
            const auto a=region.opening.getMinimum(),b=region.opening.getMaximum();
            const auto finite=[](const glm::vec3& v){
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            if (region.room.empty() || !rooms.insert(region.room).second ||
                region.sky>skies.size() || !finite(lo) || !finite(hi) ||
                !finite(a) || !finite(b) || !finite(glm::vec3(region.cylinder)) ||
                !std::isfinite(region.cylinder.w) || region.cylinder.w<0 ||
                glm::any(glm::lessThanEqual(hi,lo)) || glm::any(glm::lessThan(b,a)) ||
                glm::any(glm::lessThan(a,lo-1e-4f)) || glm::any(glm::greaterThan(b,hi+1e-4f)))
                throw std::runtime_error("Invalid room sky region");
            int flat=-1;
            for (int axis=0;axis<3;++axis){
                if (std::abs(b[axis]-a[axis])<1e-5f){
                    if (flat!=-1) throw std::runtime_error("Sky opening must be a rectangle");
                    flat=axis;
                }
            }
            if (flat<0) throw std::runtime_error("Sky opening must be planar");
            region.outward=glm::vec3(0);
            if (std::abs(a[flat]-lo[flat])<1e-4f) region.outward[flat]=-1;
            else if (std::abs(a[flat]-hi[flat])<1e-4f) region.outward[flat]=1;
            else throw std::runtime_error("Sky opening must lie on its region boundary");
        }
        m_skies=std::move(skies);m_regions=std::move(regions);
    }

    unsigned skyAt(const glm::vec3& point) const{
        for (const auto& region:m_regions) if (region.contains(point)) return region.sky;
        return 0;
    }

    unsigned skyForRay(const glm::vec3& origin,const glm::vec3& direction) const{
        for (const auto& region:m_regions){
            if (!region.contains(origin)) continue;
            float distance=0;
            if (!(glm::dot(direction,region.outward)>0 && region.crossesOpening(origin,direction,distance)))
                return region.sky;
            break;
        }
        unsigned result=0;
        float closest=std::numeric_limits<float>::infinity();
        for (const auto& region:m_regions){
            float distance=0;
            if (glm::dot(direction,region.outward)<0 &&
                region.crossesOpening(origin,direction,distance) && distance<closest){
                result=region.sky;closest=distance;
            }
        }
        return result;
    }

    const std::vector<RoomSky>& getSkies() const{ return m_skies; }
    const std::vector<SkyRegion>& getRegions() const{ return m_regions; }
private:
    std::vector<RoomSky> m_skies;
    std::vector<SkyRegion> m_regions;
};
} // namespace vr
