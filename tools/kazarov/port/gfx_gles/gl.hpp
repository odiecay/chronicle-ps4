#pragma once

// OpenGL ES 2.0 entry points, loaded at run time through the host's GetProcAddress, so the same
// backend links against desktop ES drivers (ANGLE, NVIDIA/AMD ES profiles) and the PS4 Piglet.

#ifndef GL_GLES_PROTOTYPES
#define GL_GLES_PROTOTYPES 0
#endif
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

namespace gfx::detail {

#define GFX_GL_FUNCTIONS(X)                                      \
    X(ActiveTexture, PFNGLACTIVETEXTUREPROC)                     \
    X(AttachShader, PFNGLATTACHSHADERPROC)                       \
    X(BindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC)           \
    X(BindBuffer, PFNGLBINDBUFFERPROC)                           \
    X(BindFramebuffer, PFNGLBINDFRAMEBUFFERPROC)                 \
    X(BindRenderbuffer, PFNGLBINDRENDERBUFFERPROC)               \
    X(BindTexture, PFNGLBINDTEXTUREPROC)                         \
    X(BlendColor, PFNGLBLENDCOLORPROC)                           \
    X(BlendEquationSeparate, PFNGLBLENDEQUATIONSEPARATEPROC)     \
    X(BlendFuncSeparate, PFNGLBLENDFUNCSEPARATEPROC)             \
    X(BufferData, PFNGLBUFFERDATAPROC)                           \
    X(BufferSubData, PFNGLBUFFERSUBDATAPROC)                     \
    X(CheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC)   \
    X(Clear, PFNGLCLEARPROC)                                     \
    X(ClearColor, PFNGLCLEARCOLORPROC)                           \
    X(ClearDepthf, PFNGLCLEARDEPTHFPROC)                         \
    X(ClearStencil, PFNGLCLEARSTENCILPROC)                       \
    X(ColorMask, PFNGLCOLORMASKPROC)                             \
    X(CompileShader, PFNGLCOMPILESHADERPROC)                     \
    X(CopyTexSubImage2D, PFNGLCOPYTEXSUBIMAGE2DPROC)             \
    X(CreateProgram, PFNGLCREATEPROGRAMPROC)                     \
    X(CreateShader, PFNGLCREATESHADERPROC)                       \
    X(CullFace, PFNGLCULLFACEPROC)                               \
    X(DeleteBuffers, PFNGLDELETEBUFFERSPROC)                     \
    X(DeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC)           \
    X(DeleteProgram, PFNGLDELETEPROGRAMPROC)                     \
    X(DeleteRenderbuffers, PFNGLDELETERENDERBUFFERSPROC)         \
    X(DeleteShader, PFNGLDELETESHADERPROC)                       \
    X(DeleteTextures, PFNGLDELETETEXTURESPROC)                   \
    X(DepthFunc, PFNGLDEPTHFUNCPROC)                             \
    X(DepthMask, PFNGLDEPTHMASKPROC)                             \
    X(DepthRangef, PFNGLDEPTHRANGEFPROC)                         \
    X(Disable, PFNGLDISABLEPROC)                                 \
    X(DisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC) \
    X(DrawArrays, PFNGLDRAWARRAYSPROC)                           \
    X(DrawElements, PFNGLDRAWELEMENTSPROC)                       \
    X(Enable, PFNGLENABLEPROC)                                   \
    X(EnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
    X(Finish, PFNGLFINISHPROC)                                   \
    X(FramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC) \
    X(FramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC)       \
    X(FrontFace, PFNGLFRONTFACEPROC)                             \
    X(GenBuffers, PFNGLGENBUFFERSPROC)                           \
    X(GenFramebuffers, PFNGLGENFRAMEBUFFERSPROC)                 \
    X(GenRenderbuffers, PFNGLGENRENDERBUFFERSPROC)               \
    X(GenTextures, PFNGLGENTEXTURESPROC)                         \
    X(GenerateMipmap, PFNGLGENERATEMIPMAPPROC)                   \
    X(GetError, PFNGLGETERRORPROC)                               \
    X(GetIntegerv, PFNGLGETINTEGERVPROC)                         \
    X(GetRenderbufferParameteriv, PFNGLGETRENDERBUFFERPARAMETERIVPROC) \
    X(GetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)             \
    X(GetProgramiv, PFNGLGETPROGRAMIVPROC)                       \
    X(GetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)               \
    X(GetShaderiv, PFNGLGETSHADERIVPROC)                         \
    X(GetString, PFNGLGETSTRINGPROC)                             \
    X(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)           \
    X(LinkProgram, PFNGLLINKPROGRAMPROC)                         \
    X(PixelStorei, PFNGLPIXELSTOREIPROC)                         \
    X(ReadPixels, PFNGLREADPIXELSPROC)                           \
    X(RenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC)         \
    X(Scissor, PFNGLSCISSORPROC)                                 \
    X(ShaderSource, PFNGLSHADERSOURCEPROC)                       \
    X(StencilFuncSeparate, PFNGLSTENCILFUNCSEPARATEPROC)         \
    X(StencilMaskSeparate, PFNGLSTENCILMASKSEPARATEPROC)         \
    X(StencilOpSeparate, PFNGLSTENCILOPSEPARATEPROC)             \
    X(TexImage2D, PFNGLTEXIMAGE2DPROC)                           \
    X(TexParameteri, PFNGLTEXPARAMETERIPROC)                     \
    X(TexSubImage2D, PFNGLTEXSUBIMAGE2DPROC)                     \
    X(Uniform1f, PFNGLUNIFORM1FPROC)                             \
    X(Uniform1i, PFNGLUNIFORM1IPROC)                             \
    X(Uniform2f, PFNGLUNIFORM2FPROC)                             \
    X(Uniform3f, PFNGLUNIFORM3FPROC)                             \
    X(Uniform4f, PFNGLUNIFORM4FPROC)                             \
    X(Uniform4fv, PFNGLUNIFORM4FVPROC)                           \
    X(UniformMatrix3fv, PFNGLUNIFORMMATRIX3FVPROC)               \
    X(UniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC)               \
    X(UseProgram, PFNGLUSEPROGRAMPROC)                           \
    X(VertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)         \
    X(Viewport, PFNGLVIEWPORTPROC)

struct GlFunctions {
#define X(name, type) type name = nullptr;
    GFX_GL_FUNCTIONS(X)
#undef X
};

extern GlFunctions gl;

// Capabilities the driver reports; the backend degrades where one is missing.
struct GlCaps {
    bool npot = false;                 // GL_OES_texture_npot: repeat and mips on any size
    bool depth24 = false;              // GL_OES_depth24
    bool depth32 = false;              // GL_OES_depth32
    bool packed_depth_stencil = false; // GL_OES_packed_depth_stencil
    bool element_index_uint = false;   // GL_OES_element_index_uint
    bool texture_max_level = false;    // GL_TEXTURE_MAX_LEVEL: ES 3, or GL_APPLE_texture_max_level
    bool highp_fragment = true;
    int  max_texture_size = 2048;
    int  max_vertex_attribs = 8;
    int  max_texture_units = 8;
};

extern GlCaps gl_caps;

// Loads every entry point through get_proc and fills gl_caps; false names the first missing one.
bool        GlLoad(void *(*get_proc)(const char *), const char **missing);
const char *GlErrorName(unsigned error);

} // namespace gfx::detail
