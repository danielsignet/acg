#include <vr/SkyParticles.h>
#include <vr/GLState.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace vr{
SkyParticles::SkyParticles(const std::filesystem::path& file){
    std::ifstream in(file,std::ios::binary);
    char magic[8]{};std::uint32_t version=0;
    in.read(magic,8);in.read(reinterpret_cast<char*>(&version),4);
    in.read(reinterpret_cast<char*>(&m_count),4);in.read(reinterpret_cast<char*>(&m_groups),4);
    if(std::string(magic,8)!="ACGSTARS" || version!=1 || !m_count || m_count>131072 || !m_groups || m_groups>8)
        throw std::runtime_error("Invalid sky particle catalog: "+file.string());
    for(unsigned i=0;i<m_groups;++i){
        in.read(reinterpret_cast<char*>(&m_formations[i]),64);
        in.read(reinterpret_cast<char*>(&m_speeds[i]),4);
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)
            if(!std::isfinite(m_formations[i][c][r]))throw std::runtime_error("Invalid particle formation");
        if(!std::isfinite(m_speeds[i]) || std::abs(m_speeds[i])>.1f)throw std::runtime_error("Invalid particle speed");
    }
    std::vector<float> data(static_cast<std::size_t>(m_count)*8);
    in.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()*4));
    if(!in || in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Truncated or oversized particle catalog");
    for(unsigned i=0;i<m_count;++i){
        const auto* p=&data[i*8];
        for(unsigned j=0;j<8;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite particle");
        if(p[3]<=0 || p[3]>8 || p[4]<0 || p[5]<0 || p[6]<0 || p[7]<0 || p[7]>=m_groups*8)
            throw std::runtime_error("Invalid particle attributes");
    }
    GLfloat range[2];GLState::getFloatv(GL_POINT_SIZE_RANGE,range);m_maxPointSize=range[1];
    GLint previous=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previous);
    glGenVertexArrays(1,&m_vao);glGenBuffers(1,&m_buffer);GLState::bindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER,m_buffer);glBufferData(GL_ARRAY_BUFFER,data.size()*4,data.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,32,nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,32,reinterpret_cast<void*>(16));
    GLState::bindVertexArray(previous);
}
SkyParticles::~SkyParticles(){if(m_buffer)glDeleteBuffers(1,&m_buffer);if(m_vao)GLState::deleteVertexArrays(1,&m_vao);}
void SkyParticles::draw(const Program& program,const Camera& camera,float time,float height) const{
    program.set("uSkyProjection",camera.getProjection());
    program.set("uSkyRotation",glm::mat3(camera.getView()));
    program.set("uParticleTime",time);program.set("uParticleScale",height/800.f);
    program.set("uMaxPointSize",m_maxPointSize);
    for(unsigned i=0;i<m_groups;++i)
        program.set("uFormation["+std::to_string(i)+"]",m_formations[i]*
            glm::rotate(glm::mat4(1),time*m_speeds[i],glm::vec3(0,0,1)));
    const bool pointSize=GLState::isEnabled(GL_PROGRAM_POINT_SIZE);
    GLState::enable(GL_PROGRAM_POINT_SIZE);GLState::bindVertexArray(m_vao);
    glDrawArrays(GL_POINTS,0,m_count);
    if(!pointSize)GLState::disable(GL_PROGRAM_POINT_SIZE);
}
}
