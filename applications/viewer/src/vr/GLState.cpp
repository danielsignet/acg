#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <vr/GLState.h>

namespace vr{
thread_local GLState::Cache* GLState::s_cache = nullptr;

GLState::Scope::Scope(){
    if (s_cache){
        throw std::logic_error("OpenGL state tracking already active");
    }
    s_cache = &m_cache;
}

GLState::Scope::~Scope(){
    s_cache = nullptr;
}

void GLState::invalidate(){
    if (s_cache){
        s_cache->values.clear();
        s_cache->flags.clear();
    }
}

namespace{
unsigned components(GLenum name){
    return name == GL_VIEWPORT || name == GL_COLOR_WRITEMASK ? 4 : name == GL_POLYGON_MODE ? 2 : 1;
}

template <class T> bool tracked(GLenum name){
    if constexpr (std::is_same_v<T, GLdouble>){
        return name == GL_DEPTH_CLEAR_VALUE;
    } else if constexpr (std::is_same_v<T, GLfloat>){
        return name == GL_MIN_SAMPLE_SHADING_VALUE || name == GL_POLYGON_OFFSET_FACTOR ||
               name == GL_POLYGON_OFFSET_UNITS;
    } else if constexpr (std::is_same_v<T, GLboolean>){
        return name == GL_COLOR_WRITEMASK || name == GL_DEPTH_WRITEMASK;
    } else{
        switch (name){
        case GL_DRAW_FRAMEBUFFER_BINDING:
        case GL_READ_FRAMEBUFFER_BINDING:
        case GL_VERTEX_ARRAY_BINDING:
        case GL_VIEWPORT:
        case GL_DEPTH_FUNC:
        case GL_POLYGON_MODE:
        case GL_FRONT_FACE:
        case GL_BLEND_SRC_RGB:
        case GL_BLEND_DST_RGB:
        case GL_BLEND_SRC_ALPHA:
        case GL_BLEND_DST_ALPHA:
        case GL_BLEND_EQUATION_RGB:
        case GL_BLEND_EQUATION_ALPHA:
            return true;
        default:
            return false;
        }
    }
}
} // namespace

void GLState::remember(GLenum name, std::array<GLdouble, 4> value){
    if (s_cache){
        s_cache->values[name] = value;
    }
}

template <class T> bool GLState::recall(GLenum name, T* value){
    if (s_cache){
        const auto found = s_cache->values.find(name);
        if (found != s_cache->values.end()){
            for (unsigned i = 0; i < components(name); ++i){
                value[i] = static_cast<T>(found->second[i]);
            }
            return true;
        }
    }
    return false;
}

#define VR_STATE_QUERY(method, function, type)                                                     \
    void GLState::method(GLenum name, type* value){                                               \
        if (tracked<type>(name) && recall(name, value))                                            \
            return;                                                                                \
        function(name, value);                                                                     \
        if (s_cache && tracked<type>(name)){                                                      \
            std::array<GLdouble, 4> result{};                                                      \
            for (unsigned i = 0; i < components(name); ++i)                                        \
                result[i] = value[i];                                                              \
            remember(name, result);                                                                \
        }                                                                                          \
    }
VR_STATE_QUERY(getIntegerv, glGetIntegerv, GLint)
VR_STATE_QUERY(getBooleanv, glGetBooleanv, GLboolean)
VR_STATE_QUERY(getFloatv, glGetFloatv, GLfloat)
VR_STATE_QUERY(getDoublev, glGetDoublev, GLdouble)
#undef VR_STATE_QUERY

GLboolean GLState::isEnabled(GLenum flag){
    if (s_cache){
        auto found = s_cache->flags.find(flag);
        if (found != s_cache->flags.end()){
            return found->second;
        }
        return s_cache->flags[flag] = glIsEnabled(flag);
    }
    return glIsEnabled(flag);
}

void GLState::enable(GLenum flag){
    glEnable(flag);
    if (s_cache){
        s_cache->flags[flag] = GL_TRUE;
    }
}

void GLState::disable(GLenum flag){
    glDisable(flag);
    if (s_cache){
        s_cache->flags[flag] = GL_FALSE;
    }
}

void GLState::bindFramebuffer(GLenum target, GLuint framebuffer){
    glBindFramebuffer(target, framebuffer);
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER){
        remember(GL_DRAW_FRAMEBUFFER_BINDING,{GLdouble(framebuffer)});
    }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER){
        remember(GL_READ_FRAMEBUFFER_BINDING,{GLdouble(framebuffer)});
    }
}

void GLState::deleteFramebuffers(GLsizei count, const GLuint* framebuffers){
    glDeleteFramebuffers(count, framebuffers);
    for (const auto name :{GL_DRAW_FRAMEBUFFER_BINDING, GL_READ_FRAMEBUFFER_BINDING}){
        GLuint bound = 0;
        if (recall(name, &bound) &&
            std::find(framebuffers, framebuffers + count, bound) != framebuffers + count){
            remember(name,{0});
        }
    }
}

void GLState::bindVertexArray(GLuint array){
    glBindVertexArray(array);
    remember(GL_VERTEX_ARRAY_BINDING,{GLdouble(array)});
}

void GLState::deleteVertexArrays(GLsizei count, const GLuint* arrays){
    glDeleteVertexArrays(count, arrays);
    GLuint bound = 0;
    if (recall(GL_VERTEX_ARRAY_BINDING, &bound) &&
        std::find(arrays, arrays + count, bound) != arrays + count){
        remember(GL_VERTEX_ARRAY_BINDING,{0});
    }
}

void GLState::viewport(GLint x, GLint y, GLsizei width, GLsizei height){
    glViewport(x, y, width, height);
    remember(GL_VIEWPORT,{GLdouble(x), GLdouble(y), GLdouble(width), GLdouble(height)});
}

void GLState::depthFunc(GLenum function){
    glDepthFunc(function);
    remember(GL_DEPTH_FUNC,{GLdouble(function)});
}

void GLState::depthMask(GLboolean enabled){
    glDepthMask(enabled);
    remember(GL_DEPTH_WRITEMASK,{GLdouble(enabled != GL_FALSE)});
}

void GLState::clearDepth(GLdouble value){
    glClearDepth(value);
    remember(GL_DEPTH_CLEAR_VALUE,{std::clamp(value, 0.0, 1.0)});
}

void GLState::colorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a){
    glColorMask(r, g, b, a);
    remember(GL_COLOR_WRITEMASK,{GLdouble(r != GL_FALSE), GLdouble(g != GL_FALSE),
                                  GLdouble(b != GL_FALSE), GLdouble(a != GL_FALSE)});
}

void GLState::frontFace(GLenum face){
    glFrontFace(face);
    remember(GL_FRONT_FACE,{GLdouble(face)});
}

void GLState::blendFunc(GLenum source, GLenum destination){
    blendFuncSeparate(source, destination, source, destination);
}

void GLState::blendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da){
    glBlendFuncSeparate(sr, dr, sa, da);
    remember(GL_BLEND_SRC_RGB,{GLdouble(sr)});
    remember(GL_BLEND_DST_RGB,{GLdouble(dr)});
    remember(GL_BLEND_SRC_ALPHA,{GLdouble(sa)});
    remember(GL_BLEND_DST_ALPHA,{GLdouble(da)});
}

void GLState::blendEquation(GLenum mode){
    blendEquationSeparate(mode, mode);
}

void GLState::blendEquationSeparate(GLenum rgb, GLenum alpha){
    glBlendEquationSeparate(rgb, alpha);
    remember(GL_BLEND_EQUATION_RGB,{GLdouble(rgb)});
    remember(GL_BLEND_EQUATION_ALPHA,{GLdouble(alpha)});
}

void GLState::minSampleShading(GLfloat value){
    glMinSampleShading(value);
    remember(GL_MIN_SAMPLE_SHADING_VALUE,{std::clamp(GLdouble(value), 0.0, 1.0)});
}

void GLState::polygonOffset(GLfloat factor, GLfloat units){
    glPolygonOffset(factor, units);
    remember(GL_POLYGON_OFFSET_FACTOR,{factor});
    remember(GL_POLYGON_OFFSET_UNITS,{units});
}

void GLState::polygonMode(GLenum face, GLenum mode){
    glPolygonMode(face, mode);
    if (face == GL_FRONT_AND_BACK){
        remember(GL_POLYGON_MODE,{GLdouble(mode), GLdouble(mode)});
    }
}
} // namespace vr
