#include "gl.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace gfx::detail {

GlFunctions gl;
GlCaps      gl_caps;

namespace {

bool HasExtension(const char *extensions, std::string_view name) {
    if (extensions == nullptr) {
        return false;
    }
    std::string_view list = extensions;
    size_t           pos = 0;
    while ((pos = list.find(name, pos)) != std::string_view::npos) {
        bool starts = pos == 0 || list[pos - 1] == ' ';
        bool ends = pos + name.size() == list.size() || list[pos + name.size()] == ' ';
        if (starts && ends) {
            return true;
        }
        pos += name.size();
    }
    return false;
}

} // namespace

bool GlLoad(void *(*get_proc)(const char *), const char **missing) {
    *missing = nullptr;
#define X(name, type)                                                       \
    gl.name = reinterpret_cast<type>(get_proc("gl" #name));                 \
    if (gl.name == nullptr) {                                               \
        *missing = "gl" #name;                                              \
        return false;                                                       \
    }
    GFX_GL_FUNCTIONS(X)
#undef X

    const char *extensions = reinterpret_cast<const char *>(gl.GetString(GL_EXTENSIONS));
    const char *version = reinterpret_cast<const char *>(gl.GetString(GL_VERSION));
    std::fprintf(stderr, "gfx: GL extensions: %s\n", extensions != nullptr ? extensions : "(none)");
    // An ES 3 context has all of these in core; a desktop ES 2 driver lists them as extensions.
    bool es3 = version != nullptr && std::strstr(version, "OpenGL ES 3") != nullptr;
    gl_caps.npot = es3 || HasExtension(extensions, "GL_OES_texture_npot") ||
                   HasExtension(extensions, "GL_ARB_texture_non_power_of_two");
    gl_caps.depth24 = es3 || HasExtension(extensions, "GL_OES_depth24");
    gl_caps.packed_depth_stencil = es3 || HasExtension(extensions, "GL_OES_packed_depth_stencil");
    gl_caps.depth32 = es3 || HasExtension(extensions, "GL_OES_depth32");
    // GFX_FORCE_DEPTH16=1 takes the 16-bit depth path a driver with neither GL_OES_depth24 nor
    // GL_OES_packed_depth_stencil takes (the PS4's Piglet): to reproduce what it shows, or to compare against it.
    if (std::getenv("GFX_FORCE_DEPTH16") != nullptr) {
        gl_caps.packed_depth_stencil = false;
        gl_caps.depth24 = false;
        gl_caps.depth32 = false;
    }
    gl_caps.element_index_uint = es3 || HasExtension(extensions, "GL_OES_element_index_uint");
    gl_caps.texture_max_level = es3 || HasExtension(extensions, "GL_APPLE_texture_max_level");
    // GFX_ES2_ONLY=1 behaves like a strict ES 2 driver where the backend has a fallback, to test it.
    if (std::getenv("GFX_ES2_ONLY") != nullptr) {
        gl_caps.texture_max_level = false;
    }
    GLint value = 0;
    gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
    gl_caps.max_texture_size = value > 0 ? value : 2048;
    gl.GetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value);
    gl_caps.max_vertex_attribs = value > 0 ? value : 8;
    gl.GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
    gl_caps.max_texture_units = value > 0 ? value : 8;
    return true;
}

const char *GlErrorName(unsigned error) {
    switch (error) {
        case GL_NO_ERROR:
            return "GL_NO_ERROR";
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
        default:
            return "GL error";
    }
}

} // namespace gfx::detail
