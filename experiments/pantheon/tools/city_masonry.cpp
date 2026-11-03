// Offline authoring: reuse Tower 007's actual walls for a masonry ground floor.
// Source FBX stays intact. Coordinates remain in its original centimetres.
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Vertex { aiVector3D p, n, uv; };
using Triangle = std::array<Vertex, 3>;
struct Bounds {
    aiVector3D lo{std::numeric_limits<float>::infinity()};
    aiVector3D hi{-std::numeric_limits<float>::infinity()};
    void add(const aiVector3D& p) {
        lo.x=std::min(lo.x,p.x); lo.y=std::min(lo.y,p.y); lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x); hi.y=std::max(hi.y,p.y); hi.z=std::max(hi.z,p.z);
    }
};
double area(const Triangle& t) { return .5*((t[1].p-t[0].p)^(t[2].p-t[0].p)).Length(); }
Vertex mix(const Vertex& a, const Vertex& b, float t) {
    return {a.p+(b.p-a.p)*t,a.n+(b.n-a.n)*t,a.uv+(b.uv-a.uv)*t};
}
std::vector<Vertex> clip(const Triangle& triangle, float y, bool below) {
    std::vector<Vertex> result;
    for (size_t i=0;i<3;++i) {
        const auto& a=triangle[i]; const auto& b=triangle[(i+1)%3];
        const bool insideA=below ? a.p.y<=y : a.p.y>=y;
        const bool insideB=below ? b.p.y<=y : b.p.y>=y;
        if (insideA) result.push_back(a);
        if (insideA!=insideB) result.push_back(mix(a,b,(y-a.p.y)/(b.p.y-a.p.y)));
    }
    return result;
}
}

int main(int argc, char** argv) try {
    if (argc!=4) throw std::runtime_error("Usage: city_masonry source.fbx output.obj stats.json");
    Assimp::Importer importer;
    const auto* scene=importer.ReadFile(argv[1],aiProcess_Triangulate|aiProcess_GenSmoothNormals|
        aiProcess_JoinIdenticalVertices|aiProcess_ValidateDataStructure|aiProcess_PreTransformVertices);
    if (!scene) throw std::runtime_error(importer.GetErrorString());
    if (scene->mNumAnimations) throw std::runtime_error("Only static architecture is supported");
    Bounds sourceBounds, outputBounds;
    for (unsigned m=0;m<scene->mNumMeshes;++m)
        for (unsigned v=0;v<scene->mMeshes[m]->mNumVertices;++v) sourceBounds.add(scene->mMeshes[m]->mVertices[v]);
    const float cutoff=sourceBounds.lo.y+360.0f; // Ground floor: 3.6 m above existing podium.
    std::map<std::string,std::vector<Triangle>> groups;
    size_t sourceTriangles=0, windowTriangles=0;
    double sourceArea=0, outputArea=0;
    auto emit=[&](const std::string& material, const std::vector<Vertex>& polygon, bool xWall) {
        for (size_t i=1;i+1<polygon.size();++i) {
            Triangle t{polygon[0],polygon[i],polygon[i+1]};
            if (area(t)<1e-6) continue;
            for (auto& v:t) {
                v.n.Normalize();
                if (material=="brick")
                    v.uv={(xWall ? v.p.z : v.p.x)/137.0f,(v.p.y-sourceBounds.lo.y)/137.0f,0};
                outputBounds.add(v.p);
            }
            outputArea+=area(t); groups[material].push_back(t);
        }
    };
    for (unsigned m=0;m<scene->mNumMeshes;++m) {
        const auto* mesh=scene->mMeshes[m]; aiString name;
        scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME,name);
        const std::string material=name.C_Str();
        for (unsigned f=0;f<mesh->mNumFaces;++f) {
            const auto& face=mesh->mFaces[f];
            if (face.mNumIndices!=3) throw std::runtime_error("Expected triangles");
            Triangle t;
            for (size_t j=0;j<3;++j) {
                const auto v=face.mIndices[j];
                t[j]={mesh->mVertices[v],mesh->mNormals[v],mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D{}};
            }
            ++sourceTriangles; sourceArea+=area(t);
            if (material=="hotel_glas") ++windowTriangles;
            auto n=(t[1].p-t[0].p)^(t[2].p-t[0].p); n.Normalize();
            const float low=std::min({t[0].p.y,t[1].p.y,t[2].p.y});
            if (material=="ground" && std::abs(n.y)<.25f && low<cutoff) {
                const bool xWall=std::abs(n.x)>std::abs(n.z);
                emit("brick",clip(t,cutoff,true),xWall);
                emit(material,clip(t,cutoff,false),xWall);
            } else emit(material,{t.begin(),t.end()},false);
        }
    }
    if (groups["brick"].empty() || groups["hotel_glas"].size()!=windowTriangles ||
        (sourceBounds.lo-outputBounds.lo).Length()>.01f || (sourceBounds.hi-outputBounds.hi).Length()>.01f ||
        std::abs(outputArea-sourceArea)/sourceArea>1e-6)
        throw std::runtime_error("Masonry export failed geometry/window preservation checks");

    const std::filesystem::path path=argv[2]; auto mtlPath=path; mtlPath.replace_extension(".mtl");
    std::ofstream obj(path); obj<<std::setprecision(9);
    obj<<"# Derived from ResidentialBuildings007.fbx by tools/city_masonry.cpp\nmtllib "<<mtlPath.filename().string()<<'\n';
    size_t index=1, triangles=0;
    for (const auto& [material, faces]:groups) {
        obj<<"o "<<material<<"\nusemtl "<<material<<'\n';
        for (const auto& t:faces) {
            for (const auto& v:t) obj<<"v "<<v.p.x<<' '<<v.p.y<<' '<<v.p.z<<'\n';
            for (const auto& v:t) obj<<"vt "<<v.uv.x<<' '<<v.uv.y<<'\n';
            for (const auto& v:t) obj<<"vn "<<v.n.x<<' '<<v.n.y<<' '<<v.n.z<<'\n';
            obj<<"f";
            for (size_t j=0;j<3;++j) obj<<' '<<index+j<<'/'<<index+j<<'/'<<index+j;
            obj<<'\n'; index+=3; ++triangles;
        }
    }
    std::ofstream mtl(mtlPath);
    for (unsigned m=0;m<scene->mNumMaterials;++m) {
        const auto* material=scene->mMaterials[m]; aiString name; material->Get(AI_MATKEY_NAME,name);
        mtl<<"newmtl "<<name.C_Str()<<'\n';
        for (const auto& property:std::array<std::pair<const char*,const char*>,4>{{{"Ka","$clr.ambient"},{"Kd","$clr.diffuse"},{"Ks","$clr.specular"},{"Ke","$clr.emissive"}}}) {
            aiColor3D c{0,0,0}; material->Get(property.second,0,0,c);
            mtl<<property.first<<' '<<c.r<<' '<<c.g<<' '<<c.b<<'\n';
        }
        float ns=48; material->Get(AI_MATKEY_SHININESS,ns); mtl<<"Ns "<<ns<<"\n\n";
    }
    mtl<<"newmtl brick\nKa .2 .2 .2\nKd 1 1 1\nKs .035 .035 .035\nNs 12\n";
    std::ofstream stats(argv[3]);
    stats<<std::setprecision(9)<<"{\n  \"source_triangles\": "<<sourceTriangles
         <<",\n  \"vertices\": "<<index-1<<",\n  \"triangles\": "<<triangles
         <<",\n  \"brick_triangles\": "<<groups["brick"].size()
         <<",\n  \"window_triangles\": "<<windowTriangles
         <<",\n  \"ground_floor_height_m\": 3.6,\n  \"texture_repeat_m\": 1.37,\n  \"bounds_preserved\": true,\n  \"surface_area_relative_error\": "<<std::abs(outputArea-sourceArea)/sourceArea<<"\n}\n";
    if (!obj || !mtl || !stats) throw std::runtime_error("Cannot write masonry assets");
    std::cout<<"Exported "<<triangles<<" triangles ("<<groups["brick"].size()<<" brick), preserved "<<windowTriangles<<" window triangles and source bounds\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
