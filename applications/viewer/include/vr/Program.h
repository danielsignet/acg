#pragma once
#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <variant>

namespace vr{
using Uniform =
    std::variant<bool, int, float, glm::vec2, glm::vec3, glm::vec4, glm::mat3, glm::mat4>;

class Program{
public:
    // Compiles shader stages and links them into one owned OpenGL program.
    Program(const std::filesystem::path& vertex, const std::filesystem::path& fragment,
            const std::filesystem::path& geometry ={});

    // Releases the linked OpenGL shader program.
    ~Program();

    // Prevents copying ownership or identity of this object.
    Program(const Program&) = delete;

    // Prevents copying ownership or identity of this object.
    Program& operator=(const Program&) = delete;

    // Makes this linked shader program current.
    void use() const;

    // Uploads one typed uniform, caching the shader location by name.
    void set(const std::string& name, const Uniform& value) const;

    // Returns the OpenGL identifier owned by this shader program.
    GLuint getId() const{
        return m_id;
    }

private:
    // Reads a complete shader source file and reports missing files.
    static std::string readSource(const std::filesystem::path& path);

    // Compiles one shader stage and reports the driver diagnostic on failure.
    static GLuint compileShader(const std::filesystem::path& path, GLenum type);
    GLuint m_id = 0;
    mutable std::map<std::string, GLint> m_locations;
};
} // namespace vr
