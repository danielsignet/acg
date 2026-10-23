#pragma once
#include <glad/glad.h>
#include <stdexcept>
#include <string>

namespace vr{
class OpenGL final{
public:
    // Reports an OpenGL error together with the operation that produced it.
    static void check(const char* stage){
        const auto error = glGetError();
        if (error != GL_NO_ERROR){
            throw std::runtime_error(std::string(stage) + " OpenGL error " + std::to_string(error));
        }
    }
};
} // namespace vr
