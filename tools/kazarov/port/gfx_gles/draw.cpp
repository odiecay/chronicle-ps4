#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __PS4__
#include <unistd.h>
#endif

#include "../gfx/displaylist.hpp"
#include "context.hpp"

// The draw path of the OpenGL ES 2 backend. The logical-space geometry (mappings, frame flanks and
// covers, anchors, copy rects) is gfx/draw.cpp's, unchanged; what became GL calls is the state
// setting, the draws and the copies.

namespace gfx {

namespace detail {

namespace {

// Rect and offset types the Vulkan code spelled with Vk structs, kept so its geometry reads the same.
struct PxOffset {
    int32_t x;
    int32_t y;
};
struct PxExtent {
    uint32_t width;
    uint32_t height;
};
struct PxRect {
    PxOffset offset;
    PxExtent extent;
};
struct PxOffset3 {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct Target {
    Image         *color = nullptr;
    Image         *depth = nullptr;
    Texture       *texture = nullptr;
    LogicalMapping mapping = {};
    bool           has_alpha = true;
};

Target ResolveTarget(TextureHandle handle) {
    Target target;
    if (handle == kMainTarget) {
        target.color = &MainColorTarget();
        target.depth = &MainDepth();
        target.mapping = MainMapping(target.color->width, target.color->height);
        return target;
    }
    Texture *texture = LookupTexture(handle);
    if (texture != nullptr && texture->render_target) {
        target.color = &texture->image;
        target.depth = texture->shares_main_depth ? &MainDepth() : &texture->depth;
        target.texture = texture;
        target.mapping = TextureMapping(*texture);
        target.has_alpha = texture->desc.has_alpha;
    }
    return target;
}

// ---- GL state, set only when it changes -----------------------------------------------------

void SetViewport(int width, int height) {
    width = std::max(1, width / g_tune.viewport_div);
    height = std::max(1, height / g_tune.viewport_div);
    int *v = g.bound.viewport;
    if (v[0] != 0 || v[1] != 0 || v[2] != width || v[3] != height) {
        gl.Viewport(0, 0, width, height);
        v[0] = 0;
        v[1] = 0;
        v[2] = width;
        v[3] = height;
    }
}

void SetCap(GLenum cap, int &cached, bool on) {
    if (cached != (on ? 1 : 0)) {
        if (on) {
            gl.Enable(cap);
        } else {
            gl.Disable(cap);
        }
        cached = on ? 1 : 0;
    }
}

void SetScissorRectGl(int x, int y, int w, int h) {
    SetCap(GL_SCISSOR_TEST, g.bound.scissor_test, true);
    int *s = g.bound.scissor;
    if (s[0] != x || s[1] != y || s[2] != w || s[3] != h) {
        gl.Scissor(x, y, w, h);
        s[0] = x;
        s[1] = y;
        s[2] = w;
        s[3] = h;
    }
}

void SetColorMaskGl(int mask) {
    if (g.bound.color_mask != mask) {
        gl.ColorMask((mask & kWriteRed) != 0, (mask & kWriteGreen) != 0, (mask & kWriteBlue) != 0,
                     (mask & kWriteAlpha) != 0);
        g.bound.color_mask = mask;
    }
}

void SetDepthWriteGl(bool on) {
    int value = on ? 1 : 0;
    if (g.bound.depth_write != value) {
        gl.DepthMask(on ? GL_TRUE : GL_FALSE);
        g.bound.depth_write = value;
    }
}

void UseProgramGl(GLuint program) {
    if (g.bound.program != program) {
        gl.UseProgram(program);
        g.bound.program = program;
    }
}

// 0: only attribute 0 (the blit's corners); 1: the 2D vertex layout; 2: the mesh's. Both draw layouts
// read four attributes, so only the blit needs the others off.
void SetVertexLayout(int layout) {
    if (g.bound.vertex_layout == layout) {
        return;
    }
    if (g.bound.vertex_layout < 0) {
        gl.EnableVertexAttribArray(0);
    }
    for (GLuint i = 1; i < 4; i++) {
        if (layout == 0) {
            gl.DisableVertexAttribArray(i);
        } else if (g.bound.vertex_layout <= 0) {
            gl.EnableVertexAttribArray(i);
        }
    }
    g.bound.vertex_layout = layout;
}

// What a blit or a clear leaves alone is whatever state the next draw sets again; a blit sets
// everything it relies on through these.
void BlitState() {
    SetCap(GL_BLEND, g.bound.blend, false);
    g.bound.blend_slot = -1;
    SetCap(GL_DEPTH_TEST, g.bound.depth_test, false);
    SetCap(GL_STENCIL_TEST, g.bound.stencil_test, false);
    if (g.bound.cull != 0) {
        gl.Disable(GL_CULL_FACE);
        g.bound.cull = 0;
    }
    SetColorMaskGl(0xF);
}

// dx and dy move the rect in pixels before the clamp, so an overhang past the target is shifted too.
PxRect PixelRect(const LogicalMapping &mapping, const LogicalRect &rect, float dx = 0.0f, float dy = 0.0f) {
    float x0 = std::clamp(std::floor(rect.x * mapping.scale_x + mapping.offset_x + dx), 0.0f,
                          static_cast<float>(mapping.pixel_width));
    float y0 = std::clamp(std::floor(rect.y * mapping.scale_y + mapping.offset_y + dy), 0.0f,
                          static_cast<float>(mapping.pixel_height));
    float x1 = std::clamp(std::ceil((rect.x + rect.w) * mapping.scale_x + mapping.offset_x + dx), x0,
                          static_cast<float>(mapping.pixel_width));
    float y1 = std::clamp(std::ceil((rect.y + rect.h) * mapping.scale_y + mapping.offset_y + dy), y0,
                          static_cast<float>(mapping.pixel_height));
    return PxRect{
        {static_cast<int32_t>(x0),       static_cast<int32_t>(y0)      },
        {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}
    };
}

bool SameRect(const PxRect &a, const PxRect &b) {
    return a.offset.x == b.offset.x && a.offset.y == b.offset.y && a.extent.width == b.extent.width &&
           a.extent.height == b.extent.height;
}

PxRect Intersect(const PxRect &a, const PxRect &b) {
    int32_t x0 = std::max(a.offset.x, b.offset.x);
    int32_t y0 = std::max(a.offset.y, b.offset.y);
    int32_t x1 = std::min(a.offset.x + static_cast<int32_t>(a.extent.width),
                          b.offset.x + static_cast<int32_t>(b.extent.width));
    int32_t y1 = std::min(a.offset.y + static_cast<int32_t>(a.extent.height),
                          b.offset.y + static_cast<int32_t>(b.extent.height));
    return PxRect{
        {x0, y0},
        {static_cast<uint32_t>(std::max(x1 - x0, 0)), static_cast<uint32_t>(std::max(y1 - y0, 0))}
    };
}

// Letterboxed, the bars keep what they hold: nothing drawn or cleared on the main target reaches past the frame.
bool ClipsToFrame(TextureHandle target) { return target == kMainTarget && !FillsWindow(); }

PxRect FramePixels(const LogicalMapping &mapping) { return PixelRect(mapping, LogicalFrame(kMainTarget)); }

GLenum GlCompare(CompareOp op) { return GL_NEVER + static_cast<GLenum>(op); }

GLenum GlStencilOp(StencilOp op) {
    static const GLenum kOps[8] = {GL_KEEP,    GL_ZERO, GL_REPLACE,   GL_INCR,
                                   GL_DECR,    GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
    return kOps[static_cast<int>(op)];
}

GLenum DepthFuncOf(DepthTest test) {
    switch (test) {
        case DepthTest::Never:
            return GL_NEVER;
        case DepthTest::Always:
            return GL_ALWAYS;
        case DepthTest::GEqual:
            return GL_GEQUAL;
        default:
            return GL_GREATER;
    }
}

// Faces are told apart by winding; the GL front face is set clockwise (see ResetDrawState), which is
// Vulkan's counter-clockwise on the same clip-space coordinates.
void SetStencilFace(uint32_t index, const StencilFace &face) {
    GLenum gl_face = index == 0 ? GL_FRONT : GL_BACK;
    int    ops = static_cast<int>(face.fail) | static_cast<int>(face.pass) << 4 |
              static_cast<int>(face.depth_fail) << 8;
    if (g.bound.stencil_ops[index] != ops) {
        g.bound.stencil_ops[index] = ops;
        gl.StencilOpSeparate(gl_face, GlStencilOp(face.fail), GlStencilOp(face.depth_fail), GlStencilOp(face.pass));
    }
    int func = static_cast<int>(face.compare) | face.reference << 8 | face.compare_mask << 16;
    if (g.bound.stencil_func[index] != func) {
        g.bound.stencil_func[index] = func;
        gl.StencilFuncSeparate(gl_face, GlCompare(face.compare), face.reference, face.compare_mask);
    }
    if (g.bound.stencil_mask[index] != face.write_mask) {
        g.bound.stencil_mask[index] = face.write_mask;
        gl.StencilMaskSeparate(gl_face, face.write_mask);
    }
}

void SetCullGl(CullMode cull) {
    int value = static_cast<int>(cull) == 0 ? 0 : static_cast<int>(cull); // None 0, Back 1, Front 2
    if (g.bound.cull != value) {
        if (cull == CullMode::None) {
            gl.Disable(GL_CULL_FACE);
        } else {
            if (g.bound.cull <= 0) {
                gl.Enable(GL_CULL_FACE);
            }
            gl.CullFace(cull == CullMode::Back ? GL_BACK : GL_FRONT);
        }
        g.bound.cull = value;
    }
}

void SetBlendGl(const BlendMapping &blend, const DrawState &state) {
    bool enabled = state.blend && blend.slot != 0 && !g_tune.no_blend;
    SetCap(GL_BLEND, g.bound.blend, enabled);
    if (!enabled) {
        return;
    }
    const BlendEquation &equation = g.equations[blend.slot];
    if (g.bound.blend_slot != blend.slot) {
        g.bound.blend_slot = blend.slot;
        gl.BlendEquationSeparate(equation.op, GL_FUNC_ADD);
        // The GS never blends alpha: the source alpha is what lands in the frame buffer.
        gl.BlendFuncSeparate(equation.src, equation.dst, GL_ONE, GL_ZERO);
    }
    if (equation.constant_alpha) {
        int fix = state.alpha.fix;
        if (g.bound.const_alpha != fix) {
            g.bound.const_alpha = fix;
            gl.BlendColor(0.0f, 0.0f, 0.0f, std::min(static_cast<float>(fix) / 128.0f, 1.0f));
        }
    }
}

// A span reaches an edge of the logical frame from inside it: what lies wholly in a bar (the host's
// overlay) is not carried anywhere.
bool ReachesLow(float low, float high, float edge) { return low <= edge && high > edge; }

bool ReachesHigh(float low, float high, float edge) { return high >= edge && low < edge; }

// A scissor or clear rect on the current target.
LogicalRect ExtendClearRect(const LogicalRect &rect, const LogicalMapping &mapping) {
    return ExtendRect(rect, ExtentAxes(g.target), LogicalFrame(g.target), mapping);
}

// A scissor with infinite extents reaches the current target's frame edge.
LogicalRect ScissorRect(LogicalRect rect) {
    LogicalRect frame = LogicalFrame(g.target);
    if (std::isinf(rect.w)) {
        rect.w = frame.x + frame.w - rect.x;
    }
    if (std::isinf(rect.h)) {
        rect.h = frame.y + frame.h - rect.y;
    }
    return rect;
}

// The pixels a clear reaches on the current target: its rect, or the whole target.
PxRect ClearArea(const LogicalRect *rect, const Target &target) {
    PxRect area = rect ? PixelRect(target.mapping, ExtendClearRect(*rect, target.mapping))
                         : PxRect{
                               {0,                          0                          },
                               {target.mapping.pixel_width, target.mapping.pixel_height}
    };
    return ClipsToFrame(g.target) ? Intersect(area, FramePixels(target.mapping)) : area;
}

// The pixels an anchored UI draw moves by: on each anchored axis, from the UI frame's edge to the
// edge of what the main target shows.
void UiSideShift(UiSide side, const LogicalMapping &logical, const LogicalMapping &ui, float &dx, float &dy) {
    LogicalRect visible = VisibleLogicalRect(kMainTarget);
    dx = 0.0f;
    dy = 0.0f;
    if (side.x < 0) {
        dx = visible.x * logical.scale_x + logical.offset_x - ui.offset_x;
    } else if (side.x > 0) {
        dx = (visible.x + visible.w) * logical.scale_x + logical.offset_x -
             (g.config.logical_width * ui.scale_x + ui.offset_x);
    }
    if (side.y < 0) {
        dy = visible.y * logical.scale_y + logical.offset_y - ui.offset_y;
    } else if (side.y > 0) {
        dy = (visible.y + visible.h) * logical.scale_y + logical.offset_y -
             (g.config.logical_height * ui.scale_y + ui.offset_y);
    }
    dx = std::round(dx);
    dy = std::round(dy);
}

// Binds the current target's framebuffer and viewport.
void EnsureRendering(Target &target) {
    target = ResolveTarget(g.target);
    if (target.color == nullptr) {
        g.target = kMainTarget;
        target = ResolveTarget(g.target);
    }
    BindFbo(FramebufferFor(*target.color, target.depth));
    SetViewport(static_cast<int>(target.color->width), static_cast<int>(target.color->height));
}

// Binds the program, the textures and the state a draw of this binding and state needs, and sets
// the program's per-draw uniforms. False when the draw must be dropped.
bool Prepare(PipelineFamily family, const TextureBinding &binding, const DrawState &state, const Program *&program,
             bool ui = false, UiSide side = {}) {
    if (!g.in_frame) {
        return false;
    }
    // GFX_DRAW_LIMIT=N drops every draw after the Nth of each frame, to find which draw does what.
    static const int env_limit = std::getenv("GFX_DRAW_LIMIT") ? std::atoi(std::getenv("GFX_DRAW_LIMIT")) : -1;
    const int        limit = g_tune.draw_limit >= 0 ? g_tune.draw_limit : env_limit;
    if ((family == kFamilyMesh && g_tune.skip_mesh) || (family != kFamilyMesh && g_tune.skip_2d)) {
        return false;
    }
    static uint64_t  limit_frame = 0;
    static int       limit_count = 0;
    if (limit >= 0) {
        if (limit_frame != g.frame_serial) {
            limit_frame = g.frame_serial;
            limit_count = 0;
        }
        if (++limit_count > limit) {
            return false;
        }
    }
    float       uv_xform[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    TextureMode mode = kTextureNone;
    bool        texa = false;
    Image      *texture_image = nullptr;
    Image      *palette_image = nullptr;
    if (binding.texture != kNullTexture) {
        if (binding.texture == kMainTarget || binding.texture == g.target) {
            Error("a draw samples the target it renders to; snapshot it first");
            return false;
        }
        if (binding.texture == kPreviousFrame) {
            Image         &image = PreviousFrameImage();
            LogicalMapping mapping = MainMapping(image.width, image.height);
            float          width = static_cast<float>(image.width);
            float          height = static_cast<float>(image.height);
            mode = kTextureRgba;
            texa = binding.ignore_alpha;
            texture_image = &image;
            if (family < kFamilyMesh) {
                uv_xform[0] = mapping.scale_x / width;
                uv_xform[1] = mapping.scale_y / height;
                uv_xform[2] = mapping.offset_x / width;
                uv_xform[3] = mapping.offset_y / height;
            }
        } else {
            Texture *texture = LookupTexture(binding.texture);
            if (texture == nullptr) {
                Error("a draw names texture %#x, which does not exist", binding.texture);
                return false;
            }
            texture_image = &texture->image;
            if (family < kFamilyMesh) {
                // Logical texels land on the image through its mapping, letterbox offset included.
                LogicalMapping mapping = TextureMapping(*texture);
                float          width = static_cast<float>(texture->image.width);
                float          height = static_cast<float>(texture->image.height);
                uv_xform[0] = mapping.scale_x / width;
                uv_xform[1] = mapping.scale_y / height;
                uv_xform[2] = mapping.offset_x / width;
                uv_xform[3] = mapping.offset_y / height;
            }
            if (texture->desc.format == TextureFormat::Index8) {
                Texture *palette = LookupTexture(binding.palette);
                if (palette == nullptr || palette->desc.format != TextureFormat::Rgba8) {
                    Error("index texture %#x is drawn without a palette", binding.texture);
                    return false;
                }
                palette_image = &palette->image;
                mode = kTexturePalette;
            } else {
                mode = kTextureRgba;
                texa = !texture->desc.has_alpha || binding.ignore_alpha;
            }
        }
    }

    Target target;
    EnsureRendering(target);

    BlendMapping blend = MapBlend(state.alpha, state.blend);
    program = GetProgram(family, mode, state.alpha_test);
    UseProgramGl(program->id);
    SetBlendGl(blend, state);

    // Index textures are looked up texel by texel in the shader, so their sampler never filters.
    Filter filter = mode == kTexturePalette ? Filter::Nearest : binding.filter;
    BindTexture2D(0, texture_image != nullptr ? texture_image->texture : g.textures[0].image.texture);
    if (texture_image != nullptr) {
        SetSampler(0, *texture_image, filter == Filter::Linear, binding.wrap_u, binding.wrap_v);
    }
    if (palette_image != nullptr) {
        BindTexture2D(1, palette_image->texture);
        SetSampler(1, *palette_image, false, Wrap::Clamp, Wrap::Clamp);
    }

    const LogicalMapping mapping = ui && g.target == kMainTarget ? UiMapping(target.mapping) : target.mapping;
    float                xform[4] = {2.0f * mapping.scale_x / static_cast<float>(mapping.pixel_width),
                                     2.0f * mapping.scale_y / static_cast<float>(mapping.pixel_height),
                                     2.0f * mapping.offset_x / static_cast<float>(mapping.pixel_width) - 1.0f,
                                     2.0f * mapping.offset_y / static_cast<float>(mapping.pixel_height) - 1.0f};
    float                dx = 0.0f;
    float                dy = 0.0f;
    if (ui && g.target == kMainTarget && (side.x != 0 || side.y != 0)) {
        UiSideShift(side, target.mapping, mapping, dx, dy);
        xform[2] += 2.0f * dx / static_cast<float>(mapping.pixel_width);
        xform[3] += 2.0f * dy / static_cast<float>(mapping.pixel_height);
    }
    const Program &p = *program;
    UniformCache  &uc = p.cache;
    auto           changed = [&](float *cached, const float *value, int count) {
        if (uc.valid && std::memcmp(cached, value, static_cast<size_t>(count) * sizeof(float)) == 0) {
            return false;
        }
        std::memcpy(cached, value, static_cast<size_t>(count) * sizeof(float));
        return true;
    };
    if (changed(uc.xform, xform, 4)) {
        gl.Uniform4f(p.xform, xform[0], xform[1], xform[2], xform[3]);
    }
    if (changed(uc.uv_xform, uv_xform, 4)) {
        gl.Uniform4f(p.uv_xform, uv_xform[0], uv_xform[1], uv_xform[2], uv_xform[3]);
    }
    if (texture_image != nullptr) {
        const float size[2] = {static_cast<float>(texture_image->width), static_cast<float>(texture_image->height)};
        if (changed(uc.tex_size, size, 2)) {
            gl.Uniform2f(p.tex_size, size[0], size[1]);
        }
    }
    const float scalars[11] = {state.fog ? 1.0f : 0.0f,
                               texa ? 1.0f : 0.0f,
                               texa && state.texa_aem ? 1.0f : 0.0f,
                               target.has_alpha ? 0.0f : 1.0f,
                               static_cast<float>(blend.transform),
                               blend.fix ? 1.0f : 0.0f,
                               binding.filter == Filter::Linear && !g_tune.nearest ? 1.0f : 0.0f,
                               static_cast<float>(state.alpha_ref),
                               static_cast<float>(static_cast<uint32_t>(state.alpha_func) & 7),
                               static_cast<float>(state.alpha.fix) / 128.0f,
                               static_cast<float>(state.texa_ta0) / 128.0f};
    const GLint locations[11] = {p.fog,    p.texa,      p.aem,        p.opaque_target, p.transform, p.factor_fix,
                                 p.linear, p.alpha_ref, p.alpha_func, p.fix,           p.ta0};
    for (int i = 0; i < 11; i++) {
        if (!uc.valid || uc.scalars[i] != scalars[i]) {
            uc.scalars[i] = scalars[i];
            gl.Uniform1f(locations[i], scalars[i]);
        }
    }
    const float fog_color[3] = {static_cast<float>(state.fog_color[0]) / 255.0f,
                                static_cast<float>(state.fog_color[1]) / 255.0f,
                                static_cast<float>(state.fog_color[2]) / 255.0f};
    if (changed(uc.fog_color, fog_color, 3)) {
        gl.Uniform3f(p.fog_color, fog_color[0], fog_color[1], fog_color[2]);
    }
    uc.valid = true;

    SetCullGl(state.cull);
    // An always-passing test that writes nothing is the same as no test, and cheaper.
    bool test = !(state.depth_test == DepthTest::Always && !state.depth_write);
    SetCap(GL_DEPTH_TEST, g.bound.depth_test, test);
    SetDepthWriteGl(state.depth_write);
    int op = static_cast<int>(DepthFuncOf(state.depth_test));
    if (g.bound.depth_op != op) {
        g.bound.depth_op = op;
        gl.DepthFunc(static_cast<GLenum>(op));
    }
    SetCap(GL_STENCIL_TEST, g.bound.stencil_test, state.stencil_test);
    SetStencilFace(0, state.stencil_front);
    SetStencilFace(1, state.stencil_back);
    int mask = state.color_write_mask & kWriteRgba;
    // A target without alpha keeps the alpha it was cleared to: blends that read the destination's
    // see an opaque frame, and the source alpha (the blend factor) is never stored.
    if (!target.has_alpha) {
        mask &= ~kWriteAlpha;
    }
    SetColorMaskGl(mask);

    PxRect scissor = state.scissor ? PixelRect(mapping, ExtendClearRect(ScissorRect(state.scissor_rect), mapping),
                                               side.scissor ? dx : 0.0f, side.scissor ? dy : 0.0f)
                                   : PxRect{
                                         {0,                   0                   },
                                         {mapping.pixel_width, mapping.pixel_height}
    };
    if (ClipsToFrame(g.target)) {
        scissor = Intersect(scissor, FramePixels(target.mapping));
    }
    SetScissorRectGl(scissor.offset.x, scissor.offset.y, static_cast<int>(scissor.extent.width),
                     static_cast<int>(scissor.extent.height));
    return true;
}

// Pixel rectangle of a logical one on an image, for copies and blits; along the axes in extend it
// runs edge to edge.
bool ResolveCopyRect(TextureHandle handle, const Rect &rect, uint32_t extend, Image *&image,
                     Texture *&texture, PxOffset3 offsets[2]) {
    texture = nullptr;
    LogicalMapping mapping;
    if (handle == kMainTarget || handle == kPreviousFrame) {
        image = ColorImageOf(handle);
        mapping = MainMapping(image->width, image->height);
    } else {
        texture = LookupTexture(handle);
        if (texture == nullptr) {
            Error("copy names texture %#x, which does not exist", handle);
            return false;
        }
        image = &texture->image;
        mapping = TextureMapping(*texture);
    }
    auto x = [&](int32_t value) {
        return std::clamp(
            static_cast<int32_t>(std::lround(static_cast<float>(value) * mapping.scale_x + mapping.offset_x)),
            0, static_cast<int32_t>(image->width));
    };
    auto y = [&](int32_t value) {
        return std::clamp(
            static_cast<int32_t>(std::lround(static_cast<float>(value) * mapping.scale_y + mapping.offset_y)),
            0, static_cast<int32_t>(image->height));
    };
    offsets[0] = {x(rect.x), y(rect.y), 0};
    offsets[1] = {x(rect.x + rect.w), y(rect.y + rect.h), 1};
    // A mirrored rect stays mirrored.
    if (extend & kAxisX) {
        bool mirrored = rect.w < 0;
        offsets[mirrored ? 1 : 0].x = 0;
        offsets[mirrored ? 0 : 1].x = static_cast<int32_t>(image->width);
    }
    if (extend & kAxisY) {
        bool mirrored = rect.h < 0;
        offsets[mirrored ? 1 : 0].y = 0;
        offsets[mirrored ? 0 : 1].y = static_cast<int32_t>(image->height);
    }
    return offsets[0].x != offsets[1].x && offsets[0].y != offsets[1].y;
}

bool Covers(int32_t start, int32_t length, float frame) {
    int32_t low = std::min(start, start + length);
    int32_t high = std::max(start, start + length);
    return low <= 0 && static_cast<float>(high) >= frame;
}

// Axes along which a copy runs edge to edge: both images show past their logical frames there and
// both rects cover them.
uint32_t CopyAxes(TextureHandle src, const Rect &src_rect, TextureHandle dst, const Rect &dst_rect) {
    uint32_t    axes = ExtentAxes(src) & ExtentAxes(dst);
    LogicalRect src_frame = LogicalFrame(src);
    LogicalRect dst_frame = LogicalFrame(dst);
    if (!Covers(src_rect.x, src_rect.w, src_frame.w) || !Covers(dst_rect.x, dst_rect.w, dst_frame.w)) {
        axes &= ~kAxisX;
    }
    if (!Covers(src_rect.y, src_rect.h, src_frame.h) || !Covers(dst_rect.y, dst_rect.h, dst_frame.h)) {
        axes &= ~kAxisY;
    }
    return axes;
}

struct Corners {
    float           x[2];
    float           y[2];
    const Vertex2D *at[2][2]; // [right][bottom]
};

// The quad's corners when its four vertices are those of an axis-aligned rectangle.
bool AxisAligned(const Vertex2D *quad, Corners &corners) {
    corners.x[0] = std::min({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
    corners.x[1] = std::max({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
    corners.y[0] = std::min({quad[0].y, quad[1].y, quad[2].y, quad[3].y});
    corners.y[1] = std::max({quad[0].y, quad[1].y, quad[2].y, quad[3].y});
    if (!(corners.x[1] > corners.x[0]) || !(corners.y[1] > corners.y[0])) {
        return false;
    }
    uint32_t seen = 0;
    for (int i = 0; i < 4; i++) {
        bool on_x = quad[i].x == corners.x[0] || quad[i].x == corners.x[1];
        bool on_y = quad[i].y == corners.y[0] || quad[i].y == corners.y[1];
        if (!on_x || !on_y) {
            return false;
        }
        int right = quad[i].x == corners.x[1] ? 1 : 0;
        int bottom = quad[i].y == corners.y[1] ? 1 : 0;
        corners.at[right][bottom] = &quad[i];
        seen |= 1u << (right + 2 * bottom);
    }
    return seen == 0xF;
}

float Bilerp(float a, float b, float c, float d, float tx, float ty) {
    return (a * (1.0f - tx) + b * tx) * (1.0f - ty) + (c * (1.0f - tx) + d * tx) * ty;
}

// The quad's attributes at (x, y): texture coordinates continue past its edges, colour, fog and
// depth stop at them.
Vertex2D Continue(const Corners &q, float x, float y) {
    float           tx = (x - q.x[0]) / (q.x[1] - q.x[0]);
    float           ty = (y - q.y[0]) / (q.y[1] - q.y[0]);
    float           cx = std::clamp(tx, 0.0f, 1.0f);
    float           cy = std::clamp(ty, 0.0f, 1.0f);
    const Vertex2D &a = *q.at[0][0];
    const Vertex2D &b = *q.at[1][0];
    const Vertex2D &c = *q.at[0][1];
    const Vertex2D &d = *q.at[1][1];
    Vertex2D        out = {};
    out.x = x;
    out.y = y;
    out.u = Bilerp(a.u, b.u, c.u, d.u, tx, ty);
    out.v = Bilerp(a.v, b.v, c.v, d.v, tx, ty);
    out.z = Bilerp(a.z, b.z, c.z, d.z, cx, cy);
    for (int i = 0; i < 4; i++) {
        out.color[i] = static_cast<uint8_t>(
            std::lround(Bilerp(a.color[i], b.color[i], c.color[i], d.color[i], cx, cy)));
    }
    out.fog = static_cast<uint8_t>(std::lround(Bilerp(a.fog, b.fog, c.fog, d.fog, cx, cy)));
    return out;
}

// The quads that carry each of the draw's rectangles past every edge of the logical frame it
// reaches, along the axes in axes, out to the target's edge, as Quads: retail's screen edge was the
// frame's, so whatever reached it (a fade, however it is tiled, a band, a frame grab drawn back)
// reaches the window's. The rectangles themselves are left to the draw, so what it rasterises
// inside the frame does not change.
std::vector<Vertex2D> FrameFlanks(Primitive primitive, std::span<const Vertex2D> vertices, uint32_t axes,
                                  const LogicalRect &frame, const LogicalMapping &mapping) {
    std::vector<Vertex2D> flanks;
    if (primitive != Primitive::Quads && !(primitive == Primitive::TriangleStrip && vertices.size() == 4)) {
        return flanks;
    }
    LogicalRect bounds = TargetBounds(mapping);
    float       left = bounds.x;
    float       right = bounds.x + bounds.w;
    float       top = bounds.y;
    float       bottom = bounds.y + bounds.h;
    for (size_t first = 0; first + 4 <= vertices.size(); first += 4) {
        Corners q;
        if (!AxisAligned(&vertices[first], q)) {
            continue;
        }
        bool  x = (axes & kAxisX) != 0;
        bool  y = (axes & kAxisY) != 0;
        float xs[4] = {std::min(left, q.x[0]), q.x[0], q.x[1], std::max(right, q.x[1])};
        float ys[4] = {std::min(top, q.y[0]), q.y[0], q.y[1], std::max(bottom, q.y[1])};
        int   first_column = x && ReachesLow(q.x[0], q.x[1], frame.x) ? 0 : 1;
        int   last_column = x && ReachesHigh(q.x[0], q.x[1], frame.x + frame.w) ? 2 : 1;
        int   first_row = y && ReachesLow(q.y[0], q.y[1], frame.y) ? 0 : 1;
        int   last_row = y && ReachesHigh(q.y[0], q.y[1], frame.y + frame.h) ? 2 : 1;
        for (int row = first_row; row <= last_row; row++) {
            for (int column = first_column; column <= last_column; column++) {
                if ((row == 1 && column == 1) || xs[column] == xs[column + 1] || ys[row] == ys[row + 1]) {
                    continue;
                }
                flanks.push_back(Continue(q, xs[column], ys[row]));
                flanks.push_back(Continue(q, xs[column + 1], ys[row]));
                flanks.push_back(Continue(q, xs[column + 1], ys[row + 1]));
                flanks.push_back(Continue(q, xs[column], ys[row + 1]));
            }
        }
    }
    return flanks;
}

// Whether 2D samples an image of the frame, which lies in the target's logical mapping.
bool SamplesFrame(const TextureBinding &binding) {
    if (binding.texture == kPreviousFrame) {
        return true;
    }
    const Texture *texture = LookupTexture(binding.texture);
    return texture != nullptr && (texture->shares_main_depth || texture->frame);
}

// Axes along which a 2D draw's texture continues past the logical frame: all of them untextured.
uint32_t TextureAxes(const TextureBinding &binding) {
    return binding.texture == kNullTexture ? kAxisX | kAxisY : ExtentAxes(binding.texture);
}

// The axes along which an axis-aligned rectangle covers the whole logical frame: it starts at or
// before the frame's low edge and ends at or past its high edge.
uint32_t CoveredAxes(const Corners &q, const LogicalRect &frame) {
    uint32_t axes = 0;
    if (q.x[0] <= frame.x && q.x[1] >= frame.x + frame.w) {
        axes |= kAxisX;
    }
    if (q.y[0] <= frame.y && q.y[1] >= frame.y + frame.h) {
        axes |= kAxisY;
    }
    return axes;
}

// A 2D draw that covers the whole logical frame with a texture that is an image of nothing (a
// cutscene still, a full-screen backdrop) is a cover: where the target shows past the frame, stretch
// it to the target's edge on the axes it extends, so a 4:3 still fills a wider window instead of
// leaving the frame's sides bare. An image of the frame is flanked instead (its own sides show) and
// an untextured span is solid either way, so neither stretches. Empty when nothing is stretched, so
// the caller draws the vertices as given.
std::vector<Vertex2D> StretchFrameCovers(Primitive primitive, std::span<const Vertex2D> vertices,
                                         const TextureBinding &binding, uint32_t extend,
                                         const LogicalRect &frame, const LogicalMapping &mapping) {
    std::vector<Vertex2D> stretched;
    if (extend == 0 || binding.texture == kNullTexture || TextureAxes(binding) != 0 ||
        (primitive != Primitive::Quads && !(primitive == Primitive::TriangleStrip && vertices.size() == 4))) {
        return stretched;
    }
    LogicalRect bounds = TargetBounds(mapping);
    bool        any = false;
    stretched.assign(vertices.begin(), vertices.end());
    for (size_t first = 0; first + 4 <= vertices.size(); first += 4) {
        Corners q;
        if (!AxisAligned(&vertices[first], q) || CoveredAxes(q, frame) != (kAxisX | kAxisY)) {
            continue;
        }
        for (int i = 0; i < 4; i++) {
            Vertex2D &vertex = stretched[first + i];
            if (extend & kAxisX) {
                vertex.x = bounds.x + (vertex.x - q.x[0]) / (q.x[1] - q.x[0]) * bounds.w;
            }
            if (extend & kAxisY) {
                vertex.y = bounds.y + (vertex.y - q.y[0]) / (q.y[1] - q.y[0]) * bounds.h;
            }
        }
        any = true;
    }
    if (!any) {
        stretched.clear();
    }
    return stretched;
}

void DrawPrepared(Primitive primitive, std::span<const Vertex2D> vertices, const TextureBinding &binding,
                  const DrawState &state, bool ui, UiSide side = {});

// The sides the current anchor gives a draw: where the centre of its bounds lies.
UiSide AnchoredSide(std::span<const Vertex2D> vertices) {
    const UiAnchor &anchor = g.ui_anchor;
    float           low_x = INFINITY;
    float           low_y = INFINITY;
    float           high_x = -INFINITY;
    float           high_y = -INFINITY;
    for (const Vertex2D &vertex : vertices) {
        low_x = std::min(low_x, vertex.x);
        low_y = std::min(low_y, vertex.y);
        high_x = std::max(high_x, vertex.x);
        high_y = std::max(high_y, vertex.y);
    }
    float  x = (low_x + high_x) * 0.5f;
    float  y = (low_y + high_y) * 0.5f;
    UiSide side;
    side.x = x < anchor.left_until ? -1 : x >= anchor.right_from ? 1 : 0;
    side.y = y < anchor.top_until ? -1 : y >= anchor.bottom_from ? 1 : 0;
    side.scissor = (side.x != 0 || side.y != 0) && anchor.scissor;
    return side;
}

// Copies and blits between two index textures. ES 2 cannot render to or read back a luminance
// image, so these go through the CPU copies Index8 textures keep, sampled nearest as the GPU would.
void TransferIndex(Texture &src, const PxOffset3 s[2], Texture &dst, const PxOffset3 d[2]) {
    int sw = s[1].x - s[0].x;
    int sh = s[1].y - s[0].y;
    int dw = d[1].x - d[0].x;
    int dh = d[1].y - d[0].y;
    int w = std::abs(dw);
    int h = std::abs(dh);
    int aw = std::abs(sw);
    int ah = std::abs(sh);
    if (w == 0 || h == 0 || aw == 0 || ah == 0 || src.shadow.empty() || dst.shadow.empty()) {
        return;
    }
    bool mirror_x = (sw < 0) != (dw < 0);
    bool mirror_y = (sh < 0) != (dh < 0);
    int  src_x = std::min(s[0].x, s[1].x);
    int  src_y = std::min(s[0].y, s[1].y);
    int  dst_x = std::min(d[0].x, d[1].x);
    int  dst_y = std::min(d[0].y, d[1].y);
    std::vector<uint8_t> out(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; y++) {
        int uy = mirror_y ? h - 1 - y : y;
        int sy = src_y + uy * ah / h;
        for (int x = 0; x < w; x++) {
            int ux = mirror_x ? w - 1 - x : x;
            int sx = src_x + ux * aw / w;
            out[static_cast<size_t>(y) * w + x] = src.shadow[static_cast<size_t>(sy) * src.image.width + sx];
        }
    }
    if (std::getenv("GFX_TRACE") != nullptr) {
        size_t nonzero = 0;
        for (uint8_t v : out) {
            nonzero += v != 0;
        }
        std::fprintf(stderr, "  copied %dx%d nonzero=%zu\n", w, h, nonzero);
    }
    for (int y = 0; y < h; y++) {
        std::memcpy(dst.shadow.data() + static_cast<size_t>(dst_y + y) * dst.image.width + dst_x,
                    out.data() + static_cast<size_t>(y) * w, static_cast<size_t>(w));
    }
    BindTexture2D(0, dst.image.texture);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, dst_x, dst_y, w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE, out.data());
}

// A copy or blit from src to dst, through a scratch image when both are one image, since a draw
// cannot sample the image it renders into.
void TransferImages(Image &src, const PxOffset3 s[2], Image &dst, const PxOffset3 d[2], bool linear) {
    if (&src == &dst) {
        int   width = std::abs(s[1].x - s[0].x);
        int   height = std::abs(s[1].y - s[0].y);
        Image scratch = CreateColorImage(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, false);
        BlitImages(src, std::min(s[0].x, s[1].x), std::min(s[0].y, s[1].y), std::max(s[0].x, s[1].x),
                   std::max(s[0].y, s[1].y), scratch, 0, 0, width, height, false);
        // Mirroring, if any, happens on the way back.
        int bx0 = 0;
        int bx1 = width;
        int by0 = 0;
        int by1 = height;
        if (s[1].x < s[0].x) {
            std::swap(bx0, bx1);
        }
        if (s[1].y < s[0].y) {
            std::swap(by0, by1);
        }
        BlitImages(scratch, bx0, by0, bx1, by1, dst, d[0].x, d[0].y, d[1].x, d[1].y, linear);
        DestroyImage(scratch);
        return;
    }
    BlitImages(src, s[0].x, s[0].y, s[1].x, s[1].y, dst, d[0].x, d[0].y, d[1].x, d[1].y, linear);
}

void Transfer(Image &src, Texture *src_texture, const PxOffset3 s[2], Image &dst, Texture *dst_texture,
              const PxOffset3 d[2], bool linear) {
    if (src.index8 && std::getenv("GFX_TRACE") != nullptr) {
        std::fprintf(stderr, "index transfer src %ux%u (%d,%d)-(%d,%d) dst %ux%u (%d,%d)-(%d,%d) shadows %zu/%zu\n", src.width, src.height, s[0].x, s[0].y, s[1].x, s[1].y, dst.width, dst.height, d[0].x, d[0].y, d[1].x, d[1].y, src_texture ? src_texture->shadow.size() : 0, dst_texture ? dst_texture->shadow.size() : 0);
    }
    if (src.index8) {
        if (src_texture != nullptr && dst_texture != nullptr) {
            TransferIndex(*src_texture, s, *dst_texture, d);
        }
        return;
    }
    TransferImages(src, s, dst, d, linear);
}

} // namespace

LogicalMapping MainMapping(uint32_t width, uint32_t height) {
    float w = static_cast<float>(width);
    float h = static_cast<float>(height);
    const float logical_w = g.config.logical_width;
    const float logical_h = g.config.logical_height;
    const float aspect = g.config.display_aspect;
    float       scale_x;
    float       scale_y;
    if (logical_w / logical_h == aspect) {
        // Square texels keep this expression: the general form below rounds differently.
        scale_x = std::min(w / logical_w, h / logical_h);
        scale_y = scale_x;
    } else {
        float shown_w = std::min(w, h * aspect);
        scale_x = shown_w / logical_w;
        scale_y = shown_w / aspect / logical_h;
    }
    return LogicalMapping{scale_x,
                          scale_y,
                          std::floor((w - logical_w * scale_x) * 0.5f),
                          std::floor((h - logical_h * scale_y) * 0.5f),
                          width,
                          height};
}

LogicalMapping UiMapping(const LogicalMapping &mapping) {
    float scale = g.layout.ui_scale;
    if (scale == 1.0f) {
        return mapping;
    }
    float logical_w = g.config.logical_width;
    float logical_h = g.config.logical_height;
    float centre_x = logical_w * 0.5f * mapping.scale_x + mapping.offset_x;
    float centre_y = logical_h * 0.5f * mapping.scale_y + mapping.offset_y;
    float scale_x = mapping.scale_x * scale;
    float scale_y = mapping.scale_y * scale;
    return LogicalMapping{scale_x,
                          scale_y,
                          std::round(centre_x - logical_w * 0.5f * scale_x),
                          std::round(centre_y - logical_h * 0.5f * scale_y),
                          mapping.pixel_width,
                          mapping.pixel_height};
}

bool FillsWindow() { return g.layout.aspect == AspectMode::Fill; }

LogicalRect TargetBounds(const LogicalMapping &mapping) {
    float left = -mapping.offset_x / mapping.scale_x;
    float top = -mapping.offset_y / mapping.scale_y;
    return {left, top, static_cast<float>(mapping.pixel_width) / mapping.scale_x,
            static_cast<float>(mapping.pixel_height) / mapping.scale_y};
}

uint32_t ExtentAxes(TextureHandle handle) {
    if (!FillsWindow()) {
        return 0;
    }
    if (handle == kMainTarget || handle == kPreviousFrame) {
        return kAxisX | kAxisY;
    }
    const Texture *texture = LookupTexture(handle);
    if (texture == nullptr || !texture->render_target) {
        return 0;
    }
    if (texture->shares_main_depth) {
        return kAxisX | kAxisY;
    }
    return texture->frame ? kAxisX : 0;
}

LogicalRect LogicalFrame(TextureHandle handle) {
    const Texture *texture = handle == kMainTarget ? nullptr : LookupTexture(handle);
    if (texture == nullptr) {
        return {0.0f, 0.0f, LogicalWidth(), LogicalHeight()};
    }
    return {0.0f, 0.0f, static_cast<float>(texture->logical_width),
            static_cast<float>(texture->logical_height)};
}

LogicalRect ExtendRect(LogicalRect rect, uint32_t axes, const LogicalRect &frame,
                       const LogicalMapping &mapping) {
    LogicalRect bounds = TargetBounds(mapping);
    float       left = rect.x;
    float       right = rect.x + rect.w;
    float       top = rect.y;
    float       bottom = rect.y + rect.h;
    if (axes & kAxisX) {
        float low = left;
        float high = right;
        left = ReachesLow(low, high, frame.x) ? std::min(left, bounds.x) : left;
        right = ReachesHigh(low, high, frame.x + frame.w) ? std::max(right, bounds.x + bounds.w) : right;
    }
    if (axes & kAxisY) {
        float low = top;
        float high = bottom;
        top = ReachesLow(low, high, frame.y) ? std::min(top, bounds.y) : top;
        bottom = ReachesHigh(low, high, frame.y + frame.h) ? std::max(bottom, bounds.y + bounds.h) : bottom;
    }
    return {left, top, right - left, bottom - top};
}

// No render pass to end or image to release in GL; the names stay for the display list and the
// renderer, which end and start a target the way the Vulkan path does.
void EndRendering() {}

void ResetDrawState() {
    g.bound = {};
    gl.FrontFace(GL_CW);
    gl.DepthRangef(0.0f, 1.0f);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void ReleaseTarget() {}

void BlitImages(Image &src, int sx0, int sy0, int sx1, int sy1, Image &dst, int dx0, int dy0, int dx1, int dy1,
                bool linear) {
    int w = std::abs(dx1 - dx0);
    int h = std::abs(dy1 - dy0);
    if (w == 0 || h == 0 || src.width == 0 || src.height == 0) {
        return;
    }
    BindFbo(FramebufferFor(dst, nullptr));
    gl.Viewport(std::min(dx0, dx1), std::min(dy0, dy1), w, h);
    for (int &v : g.bound.viewport) {
        v = -1;
    }
    SetCap(GL_SCISSOR_TEST, g.bound.scissor_test, false);
    BlitState();
    UseProgramGl(g.blit_program);
    SetVertexLayout(0);
    BindArrayBuffer(g.quad_buffer);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    BindTexture2D(0, src.texture);
    SetSampler(0, src, linear, Wrap::Clamp, Wrap::Clamp);
    // dst's left edge is whichever of its two corners is lower in x, and it takes that corner's source.
    float width = static_cast<float>(src.width);
    float height = static_cast<float>(src.height);
    float u_left = static_cast<float>(dx0 < dx1 ? sx0 : sx1) / width;
    float u_right = static_cast<float>(dx0 < dx1 ? sx1 : sx0) / width;
    float v_low = static_cast<float>(dy0 < dy1 ? sy0 : sy1) / height;
    float v_high = static_cast<float>(dy0 < dy1 ? sy1 : sy0) / height;
    gl.Uniform4f(g.blit_rect, -1.0f, -1.0f, 1.0f, 1.0f);
    gl.Uniform4f(g.blit_uv, u_left, v_low, u_right, v_high);
    g_stats.blits++;
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void PresentImage(Image &source) {
    g.last_presented = &source;
    BindFbo(0);
    gl.Viewport(0, 0, static_cast<GLsizei>(g.extent_width), static_cast<GLsizei>(g.extent_height));
    for (int &v : g.bound.viewport) {
        v = -1;
    }
    SetCap(GL_SCISSOR_TEST, g.bound.scissor_test, false);
    BlitState();
    UseProgramGl(g.blit_program);
    SetVertexLayout(0);
    BindArrayBuffer(g.quad_buffer);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    BindTexture2D(0, source.texture);
    SetSampler(0, source, false, Wrap::Clamp, Wrap::Clamp);
    gl.Uniform4f(g.blit_rect, -1.0f, -1.0f, 1.0f, 1.0f);
    // The image's first row is the top of the picture; the window's lowest row is its bottom.
    gl.Uniform4f(g.blit_uv, 0.0f, 1.0f, 1.0f, 0.0f);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace detail

using namespace detail;

void SetRenderTarget(TextureHandle handle) {
    if (handle == (RecordingCalls() ? g.record_target : g.target)) {
        return;
    }
    if (handle != kMainTarget) {
        Texture *texture = LookupTexture(handle);
        if (texture == nullptr || !texture->render_target) {
            Error("SetRenderTarget: %#x is not a render target", handle);
            return;
        }
    }
    if (RecordingCalls()) {
        RecordEntry(TargetEntry{handle});
        g.record_target = handle;
        return;
    }
    if (g.in_frame) {
        EndRendering();
        ReleaseTarget();
    }
    g.target = handle;
}

TextureHandle CurrentRenderTarget() { return RecordingCalls() ? g.record_target : g.target; }

LogicalMapping GetLogicalMapping(TextureHandle handle) {
    if (handle == kMainTarget || handle == kPreviousFrame) {
        const Image &image = *ColorImageOf(handle);
        return MainMapping(image.width, image.height);
    }
    Texture *texture = LookupTexture(handle);
    if (texture == nullptr) {
        return LogicalMapping{1.0f, 1.0f, 0.0f, 0.0f, 0, 0};
    }
    return TextureMapping(*texture);
}

LogicalMapping GetUiMapping(TextureHandle handle) {
    LogicalMapping mapping = GetLogicalMapping(handle);
    return handle == kMainTarget ? UiMapping(mapping) : mapping;
}

LogicalRect VisibleLogicalRect(TextureHandle handle) {
    LogicalRect frame = LogicalFrame(handle);
    uint32_t    axes = ExtentAxes(handle);
    if (axes == 0) {
        return frame;
    }
    return ExtendRect(frame, axes, frame, GetLogicalMapping(handle));
}

void SetUiAnchor(const UiAnchor &anchor) { g.ui_anchor = anchor; }

UiAnchor CurrentUiAnchor() { return g.ui_anchor; }

void Draw2D(Primitive primitive, std::span<const Vertex2D> vertices, const TextureBinding &binding,
            const DrawState &state) {
    if (vertices.empty()) {
        return;
    }
    Draw2DSided(primitive, vertices, binding, state, AnchoredSide(vertices));
}

namespace detail {

void Draw2DSided(Primitive primitive, std::span<const Vertex2D> vertices, const TextureBinding &binding,
                 const DrawState &state, UiSide side) {
    if (RecordingCalls()) {
        RecordEntry(Draw2DEntry{primitive, std::vector<Vertex2D>(vertices.begin(), vertices.end()), binding,
                                state, side});
        return;
    }
    // Depth places a draw among the meshes, and an image of the frame lies where the frame does.
    bool ui = state.depth_test == DepthTest::Always && !state.depth_write && !SamplesFrame(binding);
    if (ui && g.target == kMainTarget && (side.x != 0 || side.y != 0)) {
        // An anchored draw is a piece of the HUD, never the frame's cover: nothing flanks it.
        DrawPrepared(primitive, vertices, binding, state, ui, side);
        return;
    }
    uint32_t        extend = ExtentAxes(g.target);
    LogicalMapping  mapping = GetLogicalMapping(g.target);
    if (ui && g.target == kMainTarget) {
        mapping = UiMapping(mapping);
    }
    LogicalRect               frame = LogicalFrame(g.target);
    std::vector<Vertex2D>     stretched;
    std::span<const Vertex2D> draw = vertices;
    if (!g.host_draws) {
        stretched = StretchFrameCovers(primitive, vertices, binding, extend, frame, mapping);
        if (!stretched.empty()) {
            draw = stretched;
        }
    }
    DrawPrepared(primitive, draw, binding, state, ui);
    if (uint32_t axes = extend & TextureAxes(binding); axes != 0 && g.in_frame && !g.host_draws) {
        std::vector<Vertex2D> flanks = FrameFlanks(primitive, vertices, axes, frame, mapping);
        if (!flanks.empty()) {
            // Flanks sample past the frame's edge, where a repeating texture would wrap in its far side.
            TextureBinding clamped = binding;
            clamped.wrap_u = Wrap::Clamp;
            clamped.wrap_v = Wrap::Clamp;
            DrawPrepared(Primitive::Quads, flanks, clamped, state, ui);
        }
    }
}

namespace {

GLenum PrimitiveMode(Primitive primitive) {
    switch (primitive) {
        case Primitive::TriangleStrip:
            return GL_TRIANGLE_STRIP;
        case Primitive::TriangleFan:
            return GL_TRIANGLE_FAN;
        case Primitive::Lines:
            return GL_LINES;
        case Primitive::LineStrip:
            return GL_LINE_STRIP;
        default:
            return GL_TRIANGLES;
    }
}

bool StreamRingEnabled() {
#ifdef __PS4__
    static const bool enabled = access("/data/darkcloud/ps4_stream_ring", F_OK) == 0;
    return enabled;
#else
    return true;
#endif
}

// Streams one draw's vertices or indices through a ring buffer: each draw writes past the last one's data
// instead of reallocating the buffer per draw. The ring is allocated once (Piglet's buffer memory is small and
// a buffer orphaned while a queued draw still reads it is not given back soon: orphaning on every wrap filled
// it and every later allocation failed); at its end the writes simply start again from the front, and GL keeps
// that correct however far behind the GPU is. Returns the data's byte offset.
size_t StreamWrite(GLenum target, size_t &cursor, size_t &capacity, size_t wanted_capacity, const void *data,
                   size_t size) {
    // The ring is off unless asked for (the file /data/darkcloud/ps4_stream_ring, see StreamRingEnabled): Piglet
    // takes its buffer memory in 10 MiB chunks out of a pool the game fills almost entirely, and a ring did not
    // fit. Off, each draw's data replaces the buffer's contents the way the first version of this backend did.
    if (!StreamRingEnabled()) {
        gl.BufferData(target, static_cast<GLsizeiptr>(size), data, GL_STREAM_DRAW);
        g_stats.stream_bytes += size;
        return 0;
    }
    cursor = (cursor + 15) & ~static_cast<size_t>(15);
    if (size > capacity) {
        capacity = std::max(wanted_capacity, (size + 0xFFFF) & ~static_cast<size_t>(0xFFFF));
        gl.BufferData(target, static_cast<GLsizeiptr>(capacity), nullptr, GL_STREAM_DRAW);
        cursor = 0;
    } else if (cursor + size > capacity) {
        cursor = 0;
        g_stats.stream_orphans++; // counts wraps
    }
    gl.BufferSubData(target, static_cast<GLintptr>(cursor), static_cast<GLsizeiptr>(size), data);
    const size_t offset = cursor;
    cursor += size;
    g_stats.stream_bytes += size;
    return offset;
}

constexpr size_t kStreamVertexBytes = 4u << 20;
constexpr size_t kStreamIndexBytes = 1u << 20;

void Attributes2D(size_t base) {
    const GLsizei stride = sizeof(Vertex2D);
    auto          at = [&](size_t field) { return reinterpret_cast<const void *>(base + field); };
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex2D, x)));
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex2D, u)));
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(offsetof(Vertex2D, color)));
    gl.VertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(offsetof(Vertex2D, fog)));
}

void AttributesMesh(size_t base = 0) {
    const GLsizei stride = sizeof(Vertex3D);
    auto          at = [&](size_t field) { return reinterpret_cast<const void *>(base + field); };
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex3D, position)));
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex3D, normal)));
    gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex3D, uv)));
    gl.VertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(offsetof(Vertex3D, color)));
}

void DrawPrepared(Primitive primitive, std::span<const Vertex2D> vertices, const TextureBinding &binding,
                  const DrawState &state, bool ui, UiSide side) {
    bool           lines = primitive == Primitive::Lines || primitive == Primitive::LineStrip;
    const Program *program = nullptr;
    bool           prepared;
    {
        PhaseTimer timer(g_stats.ms_prepare);
        prepared = Prepare(lines ? kFamily2DLines : kFamily2DTriangles, binding, state, program, ui, side);
    }
    if (!prepared) {
        return;
    }
    g_stats.n_2d++;
    if (!g.batch_slots.empty()) {
        auto slot = g.batch_slots.find(vertices.data());
        if (slot != g.batch_slots.end()) {
            {
                PhaseTimer timer(g_stats.ms_data);
                SetVertexLayout(1);
                BindArrayBuffer(g.batch_vertices);
                Attributes2D(slot->second.vertex_offset);
            }
            PhaseTimer issue_timer(g_stats.ms_issue);
            g_stats.draws++;
            gl.DrawArrays(PrimitiveMode(primitive == Primitive::Quads ? Primitive::Triangles : primitive), 0,
                          static_cast<GLsizei>(slot->second.count));
            return;
        }
    }
    static std::vector<Vertex2D> expanded;
    expanded.clear();
    std::span<const Vertex2D> data = vertices;
    if (primitive == Primitive::Quads) {
        // Four vertices a quad, in order around it: the triangles (0 1 2) and (0 2 3).
        size_t quads = vertices.size() / 4;
        expanded.reserve(quads * 6);
        for (size_t i = 0; i < quads; i++) {
            const Vertex2D *q = &vertices[i * 4];
            for (int index : {0, 1, 2, 0, 2, 3}) {
                expanded.push_back(q[index]);
            }
        }
        data = expanded;
    }
    if (data.empty()) {
        return;
    }
    {
        PhaseTimer timer(g_stats.ms_data);
        SetVertexLayout(1);
        if (g_tune.client_arrays) {
            BindArrayBuffer(0);
            Attributes2D(reinterpret_cast<size_t>(data.data()));
        } else {
            BindArrayBuffer(g.stream_vertices);
            const size_t base = StreamWrite(GL_ARRAY_BUFFER, g.stream_vertex_cursor, g.stream_vertex_capacity,
                                            kStreamVertexBytes, data.data(), data.size_bytes());
            Attributes2D(base);
        }
    }
    PhaseTimer issue_timer(g_stats.ms_issue);
    g_stats.draws++;
    gl.DrawArrays(PrimitiveMode(primitive == Primitive::Quads ? Primitive::Triangles : primitive), 0,
                  static_cast<GLsizei>(data.size()));
}

} // namespace
} // namespace detail

namespace {

// A plain draw that sets kMeshProg0 is drawn plain; DrawMeshProg0 and DrawMeshImmediateProg0 are
// the way to Vu_prog0, which this backend does not have (Dark Chronicle's): they draw as plain meshes.
void ReportPlainProg0Flag(const MeshConstants &constants) {
    static bool reported = false;
    if ((constants.flags & kMeshProg0) != 0 && !reported) {
        reported = true;
        Error("a plain mesh draw sets kMeshProg0; it draws as a plain mesh");
    }
}

void ReportProg0Unsupported() {
    static bool reported = false;
    if (!reported && !g.quiet) {
        reported = true;
        Error("the OpenGL ES backend has no Vu_prog0 meshes; they draw as plain meshes");
    }
}

void SetMeshUniforms(const Program &p, const MeshConstants &c) {
    UniformCache &uc = p.cache;
    auto          changed = [&](float *cached, const float *value, int count) {
        if (uc.mesh_valid && std::memcmp(cached, value, static_cast<size_t>(count) * sizeof(float)) == 0) {
            return false;
        }
        std::memcpy(cached, value, static_cast<size_t>(count) * sizeof(float));
        return true;
    };
    if (changed(uc.mvp, c.mvp, 16)) {
        gl.UniformMatrix4fv(p.mvp, 1, GL_FALSE, c.mvp);
    }
    // Three columns of xyz and a pad each, to the 3x3 GL wants.
    const float nm[9] = {c.normal_matrix[0], c.normal_matrix[1], c.normal_matrix[2],
                         c.normal_matrix[4], c.normal_matrix[5], c.normal_matrix[6],
                         c.normal_matrix[8], c.normal_matrix[9], c.normal_matrix[10]};
    if (changed(uc.normal, nm, 9)) {
        gl.UniformMatrix3fv(p.normal_matrix, 1, GL_FALSE, nm);
    }
    float lights[32];
    std::memcpy(lights, &c.light_direction[0][0], 16 * sizeof(float));
    std::memcpy(lights + 16, &c.light_color[0][0], 16 * sizeof(float));
    if (changed(uc.lights, lights, 32)) {
        gl.Uniform4fv(p.light_direction, 4, lights);
        gl.Uniform4fv(p.light_color, 4, lights + 16);
    }
    float material[16];
    std::memcpy(material, c.ambient, 4 * sizeof(float));
    std::memcpy(material + 4, c.diffuse, 4 * sizeof(float));
    std::memcpy(material + 8, c.ambient_material, 4 * sizeof(float));
    std::memcpy(material + 12, c.fog, 4 * sizeof(float));
    if (changed(uc.material, material, 16)) {
        gl.Uniform4fv(p.ambient, 1, material);
        gl.Uniform4fv(p.diffuse, 1, material + 4);
        gl.Uniform4fv(p.ambient_material, 1, material + 8);
        gl.Uniform4fv(p.fog_params, 1, material + 12);
    }
    float clip[16];
    std::memcpy(clip, &c.clip_plane[0][0], 8 * sizeof(float));
    std::memcpy(clip + 8, &c.world_uv[0][0], 8 * sizeof(float));
    if (changed(uc.clip, clip, 16)) {
        gl.Uniform4fv(p.clip_plane, 2, clip);
        gl.Uniform4fv(p.world_uv, 2, clip + 8);
    }
    auto flag = [&](uint32_t bit) { return (c.flags & bit) != 0 ? 1.0f : 0.0f; };
    const float flags[9] = {flag(kMeshLit),   flag(kMeshVertexColor), flag(kMeshFog),      flag(kMeshShadow),
                            flag(kMeshClip0), flag(kMeshClip1),       flag(kMeshScreenUv), flag(kMeshWorldUv),
                            static_cast<float>(std::min(c.light_count, 4u))};
    if (changed(uc.flags, flags, 9)) {
        gl.Uniform1f(p.flag_lit, flags[0]);
        gl.Uniform1f(p.flag_vertex_color, flags[1]);
        gl.Uniform1f(p.flag_fog, flags[2]);
        gl.Uniform1f(p.flag_shadow, flags[3]);
        gl.Uniform1f(p.flag_clip0, flags[4]);
        gl.Uniform1f(p.flag_clip1, flags[5]);
        gl.Uniform1f(p.flag_screen_uv, flags[6]);
        gl.Uniform1f(p.flag_world_uv, flags[7]);
        gl.Uniform1f(p.light_count, flags[8]);
    }
    uc.mesh_valid = true;
}

// prog0 is the draw's Vu_prog0 block, kept for the display list; this backend draws without it.
void DrawIndexedMesh(MeshHandle handle, uint32_t first_index, uint32_t index_count, const MeshConstants &constants,
                     const Prog0Constants *prog0, const TextureBinding &binding, const DrawState &state,
                     const MeshTransform *transform) {
    Mesh *mesh = LookupMesh(handle);
    if (mesh == nullptr || first_index + index_count > mesh->index_count) {
        Error("DrawMesh: bad mesh %#x or index range", handle);
        return;
    }
    if (RecordingCalls()) {
        if (index_count != 0) {
            RecordMesh(MeshEntry{handle, first_index, index_count, {}, {}, constants, binding, state, 0}, transform,
                       prog0);
        }
        return;
    }
    if (prog0 != nullptr) {
        ReportProg0Unsupported();
    }
    const Program *program = nullptr;
    bool           prepared;
    {
        PhaseTimer timer(g_stats.ms_prepare);
        prepared = index_count != 0 && Prepare(kFamilyMesh, binding, state, program);
    }
    if (!prepared) {
        return;
    }
    g_stats.n_retained++;
    {
        PhaseTimer timer(g_stats.ms_data);
        SetMeshUniforms(*program, constants);
        SetVertexLayout(2);
        BindArrayBuffer(mesh->vertex_buffer);
        AttributesMesh();
        BindElementBuffer(mesh->index_buffer);
    }
    PhaseTimer issue_timer(g_stats.ms_issue);
    size_t index_size = mesh->index16 ? sizeof(uint16_t) : sizeof(uint32_t);
    g_stats.draws++;
    gl.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count),
                    mesh->index16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                    reinterpret_cast<const void *>(first_index * index_size));
}

void DrawImmediateMesh(std::span<const Vertex3D> vertices, std::span<const uint32_t> indices,
                       const MeshConstants &constants, const Prog0Constants *prog0, const TextureBinding &binding,
                       const DrawState &state, const MeshTransform *transform) {
    if (RecordingCalls()) {
        if (!vertices.empty() && !indices.empty()) {
            MeshEntry entry{kNullMesh, 0, 0, {}, {}, constants, binding, state, 0};
            entry.index_count = static_cast<uint32_t>(indices.size());
            entry.vertices.assign(vertices.begin(), vertices.end());
            entry.indices.assign(indices.begin(), indices.end());
            RecordMesh(std::move(entry), transform, prog0);
        }
        return;
    }
    if (prog0 != nullptr) {
        ReportProg0Unsupported();
    }
    const Program *program = nullptr;
    bool           prepared;
    {
        PhaseTimer timer(g_stats.ms_prepare);
        prepared = !vertices.empty() && !indices.empty() && Prepare(kFamilyMesh, binding, state, program);
    }
    if (!prepared) {
        return;
    }
    g_stats.n_immediate++;
    bool index16 = !gl_caps.element_index_uint;
    if (index16 && vertices.size() > 65536) {
        Error("DrawMeshImmediate: %zu vertices need GL_OES_element_index_uint", vertices.size());
        return;
    }
    const void *index_pointer = nullptr;
    GLenum      index_type = index16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    {
        PhaseTimer timer(g_stats.ms_data);
        SetMeshUniforms(*program, constants);
        SetVertexLayout(2);
        static std::vector<uint16_t> narrow;
        const void                  *index_data = indices.data();
        size_t                       index_bytes = indices.size_bytes();
        if (index16 && (g.batch_slots.empty() || g.batch_slots.find(vertices.data()) == g.batch_slots.end())) {
            narrow.resize(indices.size());
            for (size_t i = 0; i < indices.size(); i++) {
                narrow[i] = static_cast<uint16_t>(indices[i]);
            }
            index_data = narrow.data();
            index_bytes = narrow.size() * sizeof(uint16_t);
        }
        auto slot = g.batch_slots.empty() ? g.batch_slots.end() : g.batch_slots.find(vertices.data());
        if (slot != g.batch_slots.end()) {
            BindArrayBuffer(g.batch_vertices);
            AttributesMesh(slot->second.vertex_offset);
            BindElementBuffer(g.batch_indices);
            index_pointer = reinterpret_cast<const void *>(slot->second.index_offset);
        } else if (g_tune.client_arrays) {
            BindArrayBuffer(0);
            BindElementBuffer(0);
            AttributesMesh(reinterpret_cast<size_t>(vertices.data()));
            index_pointer = index_data;
        } else {
            BindArrayBuffer(g.stream_vertices);
            const size_t vertex_base = StreamWrite(GL_ARRAY_BUFFER, g.stream_vertex_cursor, g.stream_vertex_capacity,
                                                   kStreamVertexBytes, vertices.data(), vertices.size_bytes());
            AttributesMesh(vertex_base);
            BindElementBuffer(g.stream_indices);
            index_pointer = reinterpret_cast<const void *>(
                StreamWrite(GL_ELEMENT_ARRAY_BUFFER, g.stream_index_cursor, g.stream_index_capacity,
                            kStreamIndexBytes, index_data, index_bytes));
        }
    }
    PhaseTimer issue_timer(g_stats.ms_issue);
    g_stats.draws++;
    gl.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), index_type, index_pointer);
}

} // namespace

void DrawMesh(MeshHandle handle, uint32_t first_index, uint32_t index_count, const MeshConstants &constants,
              const TextureBinding &binding, const DrawState &state, const MeshTransform *transform) {
    ReportPlainProg0Flag(constants);
    DrawIndexedMesh(handle, first_index, index_count, constants, nullptr, binding, state, transform);
}

void DrawMeshProg0(MeshHandle handle, uint32_t first_index, uint32_t index_count, const MeshConstants &constants,
                   const Prog0Constants &prog0, const TextureBinding &binding, const DrawState &state,
                   const MeshTransform *transform) {
    DrawIndexedMesh(handle, first_index, index_count, constants, &prog0, binding, state, transform);
}

void DrawMeshImmediate(std::span<const Vertex3D> vertices, std::span<const uint32_t> indices,
                       const MeshConstants &constants, const TextureBinding &binding, const DrawState &state,
                       const MeshTransform *transform) {
    ReportPlainProg0Flag(constants);
    DrawImmediateMesh(vertices, indices, constants, nullptr, binding, state, transform);
}

void DrawMeshImmediateProg0(std::span<const Vertex3D> vertices, std::span<const uint32_t> indices,
                            const MeshConstants &constants, const Prog0Constants &prog0,
                            const TextureBinding &binding, const DrawState &state, const MeshTransform *transform) {
    DrawImmediateMesh(vertices, indices, constants, &prog0, binding, state, transform);
}

void Clear(bool clear_color, const uint8_t color[4], bool clear_depth, float depth, const LogicalRect *rect) {
    if (RecordingCalls() && (clear_color || clear_depth)) {
        ClearEntry entry;
        entry.color = clear_color;
        entry.depth = clear_depth;
        if (color != nullptr) {
            std::memcpy(entry.rgba, color, sizeof(entry.rgba));
        }
        entry.z = depth;
        entry.has_rect = rect != nullptr;
        entry.rect = rect ? *rect : LogicalRect{};
        RecordClear(entry);
        return;
    }
    if (!g.in_frame || (!clear_color && !clear_depth)) {
        return;
    }
    Target target;
    EnsureRendering(target);
    PxRect area = ClearArea(rect, target);
    if (area.extent.width == 0 || area.extent.height == 0) {
        return;
    }
    SetScissorRectGl(area.offset.x, area.offset.y, static_cast<int>(area.extent.width),
                     static_cast<int>(area.extent.height));
    GLbitfield mask = 0;
    if (clear_color) {
        float alpha = target.has_alpha ? std::min(static_cast<float>(color[3]) / 128.0f, 1.0f) : 1.0f;
        SetColorMaskGl(0xF);
        gl.ClearColor(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, alpha);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_depth) {
        SetDepthWriteGl(true);
        gl.ClearDepthf(std::clamp(depth, 0.0f, 1.0f));
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    gl.Clear(mask);
}

void ClearStencil(uint8_t value, const LogicalRect *rect) {
    if (RecordingCalls()) {
        ClearEntry entry;
        entry.stencil = true;
        entry.stencil_value = value;
        entry.has_rect = rect != nullptr;
        entry.rect = rect ? *rect : LogicalRect{};
        RecordClear(entry);
        return;
    }
    if (!g.in_frame) {
        return;
    }
    Target target;
    EnsureRendering(target);
    PxRect area = ClearArea(rect, target);
    if (area.extent.width == 0 || area.extent.height == 0) {
        return;
    }
    SetScissorRectGl(area.offset.x, area.offset.y, static_cast<int>(area.extent.width),
                     static_cast<int>(area.extent.height));
    gl.StencilMaskSeparate(GL_FRONT_AND_BACK, 0xFF);
    g.bound.stencil_mask[0] = g.bound.stencil_mask[1] = -1;
    gl.ClearStencil(value);
    gl.Clear(GL_STENCIL_BUFFER_BIT);
}

// GFX_TRACE=4 prints the mean colour of the two sides of every blit between colour images.
void TraceBlit(Image &src, Image &dst, const PxOffset3 s[2], const PxOffset3 d[2]) {
    static const char *trace = std::getenv("GFX_TRACE");
    if (trace == nullptr || trace[0] != '4' || src.index8 || g.frame_serial < 300) {
        return;
    }
    auto mean = [](const Image &image) {
        std::vector<uint8_t> px;
        ReadImagePixels(image, px);
        double sum[3] = {};
        for (size_t i = 0; i + 3 < px.size(); i += 4) {
            sum[0] += px[i];
            sum[1] += px[i + 1];
            sum[2] += px[i + 2];
        }
        double n = static_cast<double>(px.size() / 4);
        return std::array<double, 3>{sum[0] / n, sum[1] / n, sum[2] / n};
    };
    auto a = mean(src);
    auto b = mean(dst);
    std::fprintf(stderr, "blit %ux%u (%d,%d)-(%d,%d) -> %ux%u (%d,%d)-(%d,%d) src mean %.0f,%.0f,%.0f dst mean %.0f,%.0f,%.0f\n",
                 src.width, src.height, s[0].x, s[0].y, s[1].x, s[1].y, dst.width, dst.height, d[0].x, d[0].y, d[1].x,
                 d[1].y, a[0], a[1], a[2], b[0], b[1], b[2]);
}

bool CopyTexture(TextureHandle src, Rect src_rect, TextureHandle dst, int32_t dst_x, int32_t dst_y) {
    Image    *src_image;
    Image    *dst_image;
    Texture  *src_texture;
    Texture  *dst_texture;
    PxOffset3 src_offsets[2];
    PxOffset3 dst_offsets[2];
    Rect      dst_rect = {dst_x, dst_y, src_rect.w, src_rect.h};
    uint32_t  extend = CopyAxes(src, src_rect, dst, dst_rect);
    if (src_rect.w <= 0 || src_rect.h <= 0 ||
        !ResolveCopyRect(src, src_rect, extend, src_image, src_texture, src_offsets) ||
        !ResolveCopyRect(dst, dst_rect, extend, dst_image, dst_texture, dst_offsets)) {
        return false;
    }
    if (src_image->index8 != dst_image->index8) {
        Error("CopyTexture between an index and a colour texture");
        return false;
    }
    // Between images of one scale the copy is exact, clipped by whichever edge comes first;
    // between a scaled render target and a plain texture it becomes a nearest blit.
    LogicalMapping src_mapping = GetLogicalMapping(src);
    LogicalMapping dst_mapping = GetLogicalMapping(dst);
    if (src_mapping.scale_x == dst_mapping.scale_x && src_mapping.scale_y == dst_mapping.scale_y) {
        int32_t width = std::min(src_offsets[1].x - src_offsets[0].x, dst_offsets[1].x - dst_offsets[0].x);
        int32_t height = std::min(src_offsets[1].y - src_offsets[0].y, dst_offsets[1].y - dst_offsets[0].y);
        src_offsets[1] = {src_offsets[0].x + width, src_offsets[0].y + height, 1};
        dst_offsets[1] = {dst_offsets[0].x + width, dst_offsets[0].y + height, 1};
    }
    if (RecordingCalls()) {
        RecordEntry(CopyEntry{CopyEntry::Copy, src, src_rect, dst, dst_rect, Filter::Nearest});
        return true;
    }
    Transfer(*src_image, src_texture, src_offsets, *dst_image, dst_texture, dst_offsets, false);
    return true;
}

bool BlitTexture(TextureHandle src, Rect src_rect, TextureHandle dst, Rect dst_rect, Filter filter) {
    Image    *src_image;
    Image    *dst_image;
    Texture  *src_texture;
    Texture  *dst_texture;
    PxOffset3 src_offsets[2];
    PxOffset3 dst_offsets[2];
    uint32_t  extend = CopyAxes(src, src_rect, dst, dst_rect);
    if (!ResolveCopyRect(src, src_rect, extend, src_image, src_texture, src_offsets) ||
        !ResolveCopyRect(dst, dst_rect, extend, dst_image, dst_texture, dst_offsets)) {
        return false;
    }
    if (src_image->index8 != dst_image->index8) {
        Error("BlitTexture between an index and a colour texture");
        return false;
    }
    if (RecordingCalls()) {
        RecordEntry(CopyEntry{CopyEntry::Blit, src, src_rect, dst, dst_rect, filter});
        return true;
    }
    bool linear = filter == Filter::Linear && !src_image->index8;
    Transfer(*src_image, src_texture, src_offsets, *dst_image, dst_texture, dst_offsets, linear);
    TraceBlit(*src_image, *dst_image, src_offsets, dst_offsets);
    return true;
}

bool SnapshotFrame(TextureHandle dst) {
    std::optional<TextureInfo> info = GetTextureInfo(dst);
    if (!info || dst == kMainTarget || dst == kPreviousFrame) {
        Error("SnapshotFrame: %#x is not a texture", dst);
        return false;
    }
    return BlitTexture(kMainTarget,
                       Rect{0, 0, static_cast<int32_t>(LogicalWidth()), static_cast<int32_t>(LogicalHeight())}, dst,
                       Rect{0, 0, static_cast<int32_t>(info->width), static_cast<int32_t>(info->height)},
                       Filter::Linear);
}


namespace detail {

// Gathers the vertices and indices of every immediate mesh and the vertices of every 2D draw of a list into one
// upload each, so replaying it makes two glBufferData calls rather than two a draw. Draws whose data the replay
// replaces (interpolated frames) are not in the batch and take the per-draw path.
void BeginImmediateBatch(const void *list_pointer) {
    PhaseTimer batch_timer(g_stats.ms_batch);
    const DisplayList &list = *static_cast<const DisplayList *>(list_pointer);
    g.batch_slots.clear();
    if (g_tune.no_batch) {
        return;
    }
    static std::vector<uint8_t> vertex_bytes;
    static std::vector<uint8_t> index_bytes;
    vertex_bytes.clear();
    index_bytes.clear();
    const bool index16 = !gl_caps.element_index_uint;
    auto       align = [](std::vector<uint8_t> &bytes) { bytes.resize((bytes.size() + 15) & ~static_cast<size_t>(15)); };
    for (const Entry &entry : list.entries) {
        if (const MeshEntry *mesh = std::get_if<MeshEntry>(&entry)) {
            if (mesh->mesh != kNullMesh || mesh->vertices.empty() || mesh->indices.empty() ||
                (index16 && mesh->vertices.size() > 65536)) {
                continue;
            }
            align(vertex_bytes);
            align(index_bytes);
            BatchSlot slot;
            slot.vertex_offset = vertex_bytes.size();
            slot.index_offset = index_bytes.size();
            const uint8_t *v = reinterpret_cast<const uint8_t *>(mesh->vertices.data());
            vertex_bytes.insert(vertex_bytes.end(), v, v + mesh->vertices.size() * sizeof(Vertex3D));
            if (index16) {
                const size_t at = index_bytes.size();
                index_bytes.resize(at + mesh->indices.size() * sizeof(uint16_t));
                uint16_t *out = reinterpret_cast<uint16_t *>(index_bytes.data() + at);
                for (size_t i = 0; i < mesh->indices.size(); i++) {
                    out[i] = static_cast<uint16_t>(mesh->indices[i]);
                }
            } else {
                const uint8_t *i = reinterpret_cast<const uint8_t *>(mesh->indices.data());
                index_bytes.insert(index_bytes.end(), i, i + mesh->indices.size() * sizeof(uint32_t));
            }
            g.batch_slots[mesh->vertices.data()] = slot;
        } else if (const Draw2DEntry *quad = std::get_if<Draw2DEntry>(&entry)) {
            if (quad->vertices.empty()) {
                continue;
            }
            align(vertex_bytes);
            BatchSlot slot;
            slot.vertex_offset = vertex_bytes.size();
            if (quad->primitive == Primitive::Quads) {
                const size_t quads = quad->vertices.size() / 4;
                const size_t at = vertex_bytes.size();
                vertex_bytes.resize(at + quads * 6 * sizeof(Vertex2D));
                Vertex2D *out = reinterpret_cast<Vertex2D *>(vertex_bytes.data() + at);
                for (size_t q = 0; q < quads; q++) {
                    const Vertex2D *corner = &quad->vertices[q * 4];
                    for (int index : {0, 1, 2, 0, 2, 3}) {
                        *out++ = corner[index];
                    }
                }
                slot.count = static_cast<uint32_t>(quads * 6);
            } else {
                const uint8_t *v = reinterpret_cast<const uint8_t *>(quad->vertices.data());
                vertex_bytes.insert(vertex_bytes.end(), v, v + quad->vertices.size() * sizeof(Vertex2D));
                slot.count = static_cast<uint32_t>(quad->vertices.size());
            }
            if (slot.count != 0) {
                g.batch_slots[quad->vertices.data()] = slot;
            }
        }
    }
    if (g.batch_slots.empty()) {
        return;
    }
    if (g.batch_vertices == 0) {
        gl.GenBuffers(1, &g.batch_vertices);
        gl.GenBuffers(1, &g.batch_indices);
    }
    while (gl.GetError() != GL_NO_ERROR) {
    }
    BindArrayBuffer(g.batch_vertices);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_bytes.size()), vertex_bytes.data(), GL_STREAM_DRAW);
    BindElementBuffer(g.batch_indices);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(std::max<size_t>(index_bytes.size(), 16)),
                  index_bytes.data(), GL_STREAM_DRAW);
    if (gl.GetError() != GL_NO_ERROR) {
        // Piglet's buffer memory had no room for the batch: draw each the old way.
        static bool reported = false;
        if (!reported) {
            reported = true;
            Error("the batch buffers (%zu + %zu bytes) did not fit in GL memory; drawing without them",
                  vertex_bytes.size(), index_bytes.size());
        }
        g.batch_slots.clear();
        return;
    }
    g_stats.stream_bytes += vertex_bytes.size() + index_bytes.size();
}

void EndImmediateBatch() { g.batch_slots.clear(); }

} // namespace detail

} // namespace gfx
