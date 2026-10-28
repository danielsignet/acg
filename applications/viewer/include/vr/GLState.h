#pragma once
#include <array>
#include <glad/glad.h>
#include <map>

namespace vr{
// Tracks state while the viewer exclusively owns its current OpenGL context. Outside a
// Scope, every call retains the normal GL query behavior (including library callers).
class GLState{
    struct Cache{
        std::map<GLenum, std::array<GLdouble, 4>> values;
        std::map<GLenum, GLboolean> flags;
    };

    static thread_local Cache* s_cache;
    static void remember(GLenum name, std::array<GLdouble, 4> value);
    template <class T> static bool recall(GLenum name, T* value);

public:
    class Scope{
    public:
        Scope();
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Cache m_cache;
    };

    // Call after external code changes the owned context without these entry points.
    static void invalidate();
    static void getIntegerv(GLenum name, GLint* value);
    static void getBooleanv(GLenum name, GLboolean* value);
    static void getFloatv(GLenum name, GLfloat* value);
    static void getDoublev(GLenum name, GLdouble* value);
    static GLboolean isEnabled(GLenum flag);
    static void enable(GLenum flag);
    static void disable(GLenum flag);
    static void bindFramebuffer(GLenum target, GLuint framebuffer);
    static void deleteFramebuffers(GLsizei count, const GLuint* framebuffers);
    static void bindVertexArray(GLuint array);
    static void useProgram(GLuint program);
    static void bindTransformFeedback(GLuint feedback);
    static void deleteVertexArrays(GLsizei count, const GLuint* arrays);
    static void viewport(GLint x, GLint y, GLsizei width, GLsizei height);
    static void depthFunc(GLenum function);
    static void depthMask(GLboolean enabled);
    static void clearDepth(GLdouble value);
    static void colorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
    static void frontFace(GLenum face);
    static void blendFunc(GLenum source, GLenum destination);
    static void blendFuncSeparate(GLenum sourceRgb, GLenum destinationRgb, GLenum sourceAlpha,
                                  GLenum destinationAlpha);
    static void blendEquation(GLenum mode);
    static void blendEquationSeparate(GLenum rgb, GLenum alpha);
    static void minSampleShading(GLfloat value);
    static void polygonOffset(GLfloat factor, GLfloat units);
    static void polygonMode(GLenum face, GLenum mode);
};
} // namespace vr
