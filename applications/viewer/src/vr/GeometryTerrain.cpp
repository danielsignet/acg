#include <vr/Geometry.h>
#include <cmath>
#include <stdexcept>

namespace vr{
void Geometry::setHeightField(std::shared_ptr<Texture> height, const glm::vec3& dimensions,
                              unsigned columns,unsigned rows){
    if(!height || height->isRenderTarget() || columns<1 || rows<1 || columns>512 || rows>512 ||
       !std::isfinite(dimensions.x) || !std::isfinite(dimensions.y) || !std::isfinite(dimensions.z) ||
       dimensions.x<=0 || dimensions.y<=0 || dimensions.z<=0)
        throw std::invalid_argument("Invalid immutable terrain height field");
    m_heightField=std::move(height);m_heightDimensions=dimensions;
    std::vector<glm::vec3> positions,normals;
    std::vector<glm::vec2> uv;
    std::vector<unsigned> indices;
    const glm::vec2 texel(1.f/m_heightField->getWidth(),1.f/m_heightField->getHeight());
    const auto sample=[&](glm::vec2 p){
        return dimensions.y*m_heightField->sampleAlpha(glm::clamp(p,texel*.5f,glm::vec2(1)-texel*.5f));
    };
    for(unsigned y=0;y<=rows;++y) for(unsigned x=0;x<=columns;++x){
        const glm::vec2 t(float(x)/columns,float(y)/rows);
        positions.push_back({(t.x-.5f)*dimensions.x,sample(t),(t.y-.5f)*dimensions.z});
        const float dx=(sample(t+glm::vec2(texel.x,0))-sample(t-glm::vec2(texel.x,0)))/(2*texel.x*dimensions.x);
        const float dz=(sample(t+glm::vec2(0,texel.y))-sample(t-glm::vec2(0,texel.y)))/(2*texel.y*dimensions.z);
        normals.push_back(glm::normalize(glm::vec3(-dx,1,-dz)));uv.push_back(t);
    }
    for(unsigned y=0;y<rows;++y) for(unsigned x=0;x<columns;++x){
        const auto a=y*(columns+1)+x,b=a+1,c=a+columns+1,d=c+1;
        indices.insert(indices.end(),{a,c,b,b,c,d});
    }
    setData(std::move(positions),std::move(indices),std::move(normals),std::move(uv));
}
void Geometry::applyHeightField(const Program& program) const{
    if(!m_heightField)return;
    m_heightField->apply(3);
    program.set("uTerrainHeight",3);program.set("uTerrainDimensions",m_heightDimensions);
}
}
