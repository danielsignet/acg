#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <vr/Program.h>

namespace vr{
// Reads shader source and expands relative includes with bounded nesting.
std::string Program::readSource(const std::filesystem::path& path, unsigned depth){
    if (depth > 16){
        throw std::runtime_error("Shader include nesting too deep: " + path.string());
    }
    std::ifstream stream(path);
    if (!stream){
        throw std::runtime_error("Cannot read " + path.string());
    }
    std::ostringstream result;
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)){
        ++lineNumber;
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 8, "#include") == 0){
            const auto begin = line.find('"', first + 8);
            const auto end = begin == std::string::npos ? begin : line.find('"', begin + 1);
            if (end == std::string::npos){
                throw std::runtime_error("Invalid shader include: " + path.string());
            }
            result << "\n#line 1\n"
                   << readSource(path.parent_path() / line.substr(begin + 1, end - begin - 1),
                                 depth + 1)
                   << "\n#line " << lineNumber + 1 << '\n';
        } else{
            result << line << '\n';
        }
    }
    return result.str();
}

// Compiles one shader stage and reports the driver diagnostic on failure.
GLuint Program::compileShader(const std::filesystem::path& path, GLenum type){
    const auto text = readSource(path);
    const char* source = text.c_str();
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status){
        char log[8192]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(path.string() + ": " + log);
    }
    return shader;
}

// Compiles shader stages and links them into one owned OpenGL program.
Program::Program(const std::filesystem::path& vertex, const std::filesystem::path& fragment,
                 const std::filesystem::path& geometry){
    std::vector<GLuint> shaders;
    try{
        shaders.push_back(compileShader(vertex, GL_VERTEX_SHADER));
        shaders.push_back(compileShader(fragment, GL_FRAGMENT_SHADER));
        if (!geometry.empty()){
            shaders.push_back(compileShader(geometry, GL_GEOMETRY_SHADER));
        }
        m_id = glCreateProgram();
        for (auto shader : shaders){
            glAttachShader(m_id, shader);
        }
        glLinkProgram(m_id);
        GLint status = 0;
        glGetProgramiv(m_id, GL_LINK_STATUS, &status);
        if (!status){
            char log[8192]{};
            glGetProgramInfoLog(m_id, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("Shader link failed: ") + log);
        }
        for (auto shader : shaders){
            glDeleteShader(shader);
        }
    } catch (...){
        for (auto shader : shaders){
            glDeleteShader(shader);
        }
        if (m_id){
            glDeleteProgram(m_id);
        }
        throw;
    }
}

// Releases the linked OpenGL shader program.
Program::~Program(){
    if (m_id){
        glDeleteProgram(m_id);
    }
}

// Makes this linked shader program current.
void Program::use() const{
    glUseProgram(m_id);
}

// Uploads one typed uniform, caching the shader location by name.
void Program::set(const std::string& name, const Uniform& value) const{
    auto found = m_locations.find(name);
    if (found == m_locations.end()){
        found = m_locations.emplace(name, glGetUniformLocation(m_id, name.c_str())).first;
    }
    const GLint location = found->second;
    if (location < 0){
        return; // A linked program may optimise unused uniforms away.
    }
    std::visit(
        [location](const auto& v){
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int>){
                glUniform1i(location, static_cast<int>(v));
            } else if constexpr (std::is_same_v<T, float>){
                glUniform1f(location, v);
            } else if constexpr (std::is_same_v<T, glm::vec2>){
                glUniform2fv(location, 1, glm::value_ptr(v));
            } else if constexpr (std::is_same_v<T, glm::vec3>){
                glUniform3fv(location, 1, glm::value_ptr(v));
            } else if constexpr (std::is_same_v<T, glm::vec4>){
                glUniform4fv(location, 1, glm::value_ptr(v));
            } else if constexpr (std::is_same_v<T, glm::mat3>){
                glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(v));
            } else if constexpr (std::is_same_v<T, glm::mat4>){
                glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(v));
            }
        },
        value);
}
} // namespace vr
