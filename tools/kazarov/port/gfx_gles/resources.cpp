#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../gfx/displaylist.hpp"
#include "context.hpp"

namespace gfx {

namespace detail {

namespace {

constexpr uint32_t kSlotBits = 16;
constexpr uint32_t kSlotMask = (1u << kSlotBits) - 1;

uint32_t MakeHandle(uint16_t generation, uint32_t slot) {
    return (static_cast<uint32_t>(generation) << kSlotBits) | slot;
}

uint16_t NextGeneration(uint16_t generation) { return generation == 0xFFFF ? 1 : generation + 1; }

uint32_t TexelSize(TextureFormat format) { return format == TextureFormat::Index8 ? 1 : 4; }

bool IsPowerOfTwo(uint32_t value) { return value != 0 && (value & (value - 1)) == 0; }

uint32_t AllocateTextureSlot() {
    if (!g.free_texture_slots.empty()) {
        uint32_t slot = g.free_texture_slots.back();
        g.free_texture_slots.pop_back();
        return slot;
    }
    if (g.textures.size() >= kMaxTextures) {
        return 0;
    }
    g.textures.emplace_back();
    return static_cast<uint32_t>(g.textures.size() - 1);
}

bool CheckSize(uint32_t width, uint32_t height) {
    uint32_t limit = static_cast<uint32_t>(gl_caps.max_texture_size);
    if (width == 0 || height == 0 || width > limit || height > limit) {
        Error("texture size %ux%u is outside what the device supports (1..%u)", width, height, limit);
        return false;
    }
    return true;
}

Image CreateTargetImage(uint32_t width, uint32_t height) { return CreateColorImage(width, height, 1, false, false); }

Image CreateTargetDepth(uint32_t width, uint32_t height) { return CreateDepthImage(width, height); }

uint32_t ScaledSize(uint32_t logical, float scale) {
    return std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(logical) * scale)));
}

// What a render target's image measures now: the main target's for one sharing its depth, the main
// target's width and the main frame's scale in rows for a frame target filling the window, its
// logical size at the render scale otherwise.
void TargetPixelSize(const Texture &texture, uint32_t &width, uint32_t &height) {
    const Image &main = g.main_color[0];
    if (texture.shares_main_depth) {
        width = main.width;
        height = main.height;
    } else if (FrameShaped(texture)) {
        width = main.width;
        height = ScaledSize(texture.logical_height, MainMapping(main.width, main.height).scale_y);
    } else {
        width = ScaledSize(texture.logical_width, g.render_scale);
        height = ScaledSize(texture.logical_height, g.render_scale);
    }
}

// The pixel rect a mapping puts a logical rect on, for blits.
void MappedRect(const LogicalMapping &mapping, float w, float h, int rect[4]) {
    auto x = [&](float value) {
        return std::clamp(static_cast<int32_t>(std::lround(value * mapping.scale_x + mapping.offset_x)), 0,
                          static_cast<int32_t>(mapping.pixel_width));
    };
    auto y = [&](float value) {
        return std::clamp(static_cast<int32_t>(std::lround(value * mapping.scale_y + mapping.offset_y)), 0,
                          static_cast<int32_t>(mapping.pixel_height));
    };
    rect[0] = x(0.0f);
    rect[1] = y(0.0f);
    rect[2] = x(w);
    rect[3] = y(h);
}

} // namespace

// ---- Bound state helpers -----------------------------------------------------------------------

void BindFbo(GLuint fbo) {
    if (g.bound.fbo != fbo) {
        g_stats.fbo_binds++;
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        g.bound.fbo = fbo;
    }
}

void BindArrayBuffer(GLuint buffer) {
    if (g.bound.array_buffer != buffer) {
        gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
        g.bound.array_buffer = buffer;
    }
}

void BindElementBuffer(GLuint buffer) {
    if (g.bound.element_buffer != buffer) {
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
        g.bound.element_buffer = buffer;
    }
}

void BindTexture2D(uint32_t unit, GLuint texture) {
    // The unit is made active even when the texture is already on it: uploads and parameter calls
    // that follow act on whatever the active unit holds.
    if (g.bound.active_unit != static_cast<int>(unit)) {
        gl.ActiveTexture(GL_TEXTURE0 + unit);
        g.bound.active_unit = static_cast<int>(unit);
    }
    if (g.bound.texture[unit] == texture) {
        return;
    }
    gl.BindTexture(GL_TEXTURE_2D, texture);
    g.bound.texture[unit] = texture;
}

void SetSampler(uint32_t unit, Image &image, bool linear, Wrap wrap_u, Wrap wrap_v) {
    if (g.bound.active_unit != static_cast<int>(unit)) {
        gl.ActiveTexture(GL_TEXTURE0 + unit);
        g.bound.active_unit = static_cast<int>(unit);
    }
    // Without GL_OES_texture_npot ES 2 samples a non-power-of-two image only clamped and unmipped.
    bool restricted = !gl_caps.npot && (!IsPowerOfTwo(image.width) || !IsPowerOfTwo(image.height));
    bool mipped = image.mips > 1 && !restricted;
    GLint min_filter = mipped ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
                              : (linear ? GL_LINEAR : GL_NEAREST);
    GLint mag_filter = linear ? GL_LINEAR : GL_NEAREST;
    GLint wrap_s = wrap_u == Wrap::Repeat && !restricted ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    GLint wrap_t = wrap_v == Wrap::Repeat && !restricted ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    // The texture is bound to unit 0 or 1 by the caller; the parameters follow whichever is active.
    if (image.min_filter != min_filter) {
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
        image.min_filter = min_filter;
    }
    if (image.mag_filter != mag_filter) {
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
        image.mag_filter = mag_filter;
    }
    if (image.wrap_s != wrap_s) {
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
        image.wrap_s = wrap_s;
    }
    if (image.wrap_t != wrap_t) {
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
        image.wrap_t = wrap_t;
    }
}

// ---- Images ----------------------------------------------------------------------------------

Image CreateColorImage(uint32_t width, uint32_t height, uint32_t mips, bool index8, bool zero) {
    Image image;
    image.width = width;
    image.height = height;
    image.mips = mips;
    image.index8 = index8;
    gl.GenTextures(1, &image.texture);
    BindTexture2D(0, image.texture);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLenum               format = index8 ? GL_LUMINANCE : GL_RGBA;
    // Render targets are cleared right after they are made, so they are not filled with zeros here first.
    std::vector<uint8_t> zeros(zero ? static_cast<size_t>(width) * height * (index8 ? 1 : 4) : 0, 0);
    // ES 2 wants a mipmapped texture complete down to 1x1, where Vulkan clamps to the levels it has:
    // allocate the whole chain, and stop sampling at the last real level where the driver can.
    uint32_t levels = mips > 1 ? static_cast<uint32_t>(std::bit_width(std::max(width, height))) : 1;
    for (uint32_t level = 0; level < levels; level++) {
        uint32_t w = std::max(1u, width >> level);
        uint32_t h = std::max(1u, height >> level);
        gl.TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), static_cast<GLint>(format), static_cast<GLsizei>(w),
                      static_cast<GLsizei>(h), 0, format, GL_UNSIGNED_BYTE, zero ? zeros.data() : nullptr);
    }
    if (mips > 1 && gl_caps.texture_max_level) {
        gl.TexParameteri(GL_TEXTURE_2D, 0x813D /* GL_TEXTURE_MAX_LEVEL */, static_cast<GLint>(mips - 1));
    }
    // Defaults are overwritten by the first SetSampler; a mipped texture is incomplete until then.
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    image.min_filter = mips > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
    image.mag_filter = GL_NEAREST;
    image.wrap_s = GL_CLAMP_TO_EDGE;
    image.wrap_t = GL_CLAMP_TO_EDGE;
    CheckGl("CreateColorImage");
    return image;
}

Image CreateDepthImage(uint32_t width, uint32_t height, int want_bits) {
    Image image;
    image.width = width;
    image.height = height;
    gl.GenRenderbuffers(1, &image.depth_rb);
    gl.BindRenderbuffer(GL_RENDERBUFFER, image.depth_rb);
    if (gl_caps.packed_depth_stencil) {
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, static_cast<GLsizei>(width),
                               static_cast<GLsizei>(height));
        image.has_stencil = true;
    } else {
        const GLenum format = want_bits == 32 ? GL_DEPTH_COMPONENT32_OES
                              : want_bits == 24 || gl_caps.depth24 ? GL_DEPTH_COMPONENT24_OES
                                                                   : GL_DEPTH_COMPONENT16;
        gl.RenderbufferStorage(GL_RENDERBUFFER, format, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        gl.GenRenderbuffers(1, &image.stencil_rb);
        gl.BindRenderbuffer(GL_RENDERBUFFER, image.stencil_rb);
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, static_cast<GLsizei>(width),
                               static_cast<GLsizei>(height));
        image.has_stencil = true;
    }
    GLint bits = 0;
    gl.BindRenderbuffer(GL_RENDERBUFFER, image.depth_rb);
    gl.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &bits);
    image.depth_bits = static_cast<int>(bits);
    CheckGl("CreateDepthImage");
    return image;
}

// Whether `depth` makes a complete framebuffer with `color`, on a framebuffer made and dropped for the test.
static bool DepthAttachable(const Image &color, const Image &depth) {
    GLint previous = 0;
    gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    GLuint fbo = 0;
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.texture, 0);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth.depth_rb);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                               depth.stencil_rb != 0 ? depth.stencil_rb : depth.depth_rb);
    const bool complete = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl.BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    gl.DeleteFramebuffers(1, &fbo);
    return complete;
}

Image CreateSceneDepth(const Image &color, uint32_t width, uint32_t height) {
    if (!gl_caps.packed_depth_stencil && std::getenv("GFX_FORCE_DEPTH16") == nullptr) {
        for (int want : {24, 32}) {
            Image image = CreateDepthImage(width, height, want);
            const bool usable = image.depth_bits >= want && DepthAttachable(color, image);
            std::fprintf(stderr, "gfx: scene depth, %d-bit renderbuffer asked for: the driver gave %d bits, framebuffer %s\n",
                         want, image.depth_bits, usable ? "complete: used" : "not usable: dropped");
            if (usable) {
                return image;
            }
            DestroyImage(image);
        }
    }
    Image image = CreateDepthImage(width, height);
    std::fprintf(stderr, "gfx: scene depth %ux%u: %d bits%s\n", width, height, image.depth_bits,
                 gl_caps.packed_depth_stencil ? " (packed with stencil)" : " (separate stencil)");
    return image;
}

void ForgetFramebuffers(const Image &image) {
    for (auto it = g.framebuffers.begin(); it != g.framebuffers.end();) {
        GLuint color = static_cast<GLuint>(it->first >> 32);
        GLuint depth = static_cast<GLuint>(it->first & 0xFFFFFFFFu);
        bool   uses = (image.texture != 0 && color == image.texture) || (image.depth_rb != 0 && depth == image.depth_rb);
        if (!uses) {
            ++it;
            continue;
        }
        if (g.bound.fbo == it->second) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            g.bound.fbo = 0;
        }
        gl.DeleteFramebuffers(1, &it->second);
        it = g.framebuffers.erase(it);
    }
}

void DestroyImage(Image &image) {
    ForgetFramebuffers(image);
    if (image.texture != 0) {
        for (GLuint &bound : g.bound.texture) {
            if (bound == image.texture) {
                bound = 0;
            }
        }
        gl.DeleteTextures(1, &image.texture);
    }
    if (image.depth_rb != 0) {
        gl.DeleteRenderbuffers(1, &image.depth_rb);
    }
    if (image.stencil_rb != 0) {
        gl.DeleteRenderbuffers(1, &image.stencil_rb);
    }
    image = {};
}

GLuint FramebufferFor(const Image &color, const Image *depth) {
    uint64_t key = static_cast<uint64_t>(color.texture) << 32 | (depth != nullptr ? depth->depth_rb : 0);
    auto     it = g.framebuffers.find(key);
    if (it != g.framebuffers.end()) {
        return it->second;
    }
    GLuint fbo = 0;
    gl.GenFramebuffers(1, &fbo);
    BindFbo(fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.texture, 0);
    if (depth != nullptr) {
        if (depth->stencil_rb == 0) {
            // A packed depth-stencil renderbuffer goes on both attachment points in ES 2.
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth->depth_rb);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth->depth_rb);
        } else {
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth->depth_rb);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth->stencil_rb);
        }
    }
    GLenum status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Error("framebuffer %ux%u is incomplete (%#x)", color.width, color.height, status);
    }
    g.framebuffers.emplace(key, fbo);
    return fbo;
}

void ClearColorImage(Image &image, float r, float green, float b, float a) {
    BindFbo(FramebufferFor(image, nullptr));
    if (g.bound.scissor_test != 0) {
        gl.Disable(GL_SCISSOR_TEST);
        g.bound.scissor_test = 0;
    }
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g.bound.color_mask = 0xF;
    gl.ClearColor(r, green, b, a);
    gl.Clear(GL_COLOR_BUFFER_BIT);
}

void ClearDepthImage(Image &color, Image &depth) {
    BindFbo(FramebufferFor(color, &depth));
    if (g.bound.scissor_test != 0) {
        gl.Disable(GL_SCISSOR_TEST);
        g.bound.scissor_test = 0;
    }
    gl.DepthMask(GL_TRUE);
    g.bound.depth_write = 1;
    gl.StencilMaskSeparate(GL_FRONT_AND_BACK, 0xFF);
    g.bound.stencil_mask[0] = g.bound.stencil_mask[1] = -1;
    gl.ClearDepthf(0.0f);
    gl.ClearStencil(0);
    gl.Clear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

// ---- Textures -----------------------------------------------------------------------------------

bool FrameShaped(const Texture &texture) { return texture.frame && FillsWindow(); }

LogicalMapping TextureMapping(const Texture &texture) {
    if (texture.shares_main_depth) {
        return MainMapping(texture.image.width, texture.image.height);
    }
    if (FrameShaped(texture)) {
        // The logical width lies over the main target's logical frame, whose sides it keeps.
        LogicalMapping main = MainMapping(g.main_color[0].width, g.main_color[0].height);
        float          rows = static_cast<float>(texture.image.height) / static_cast<float>(texture.logical_height);
        return LogicalMapping{main.scale_x * g.config.logical_width / static_cast<float>(texture.logical_width),
                              rows,
                              main.offset_x,
                              0.0f,
                              texture.image.width,
                              texture.image.height};
    }
    return LogicalMapping{static_cast<float>(texture.image.width) / static_cast<float>(texture.logical_width),
                          static_cast<float>(texture.image.height) / static_cast<float>(texture.logical_height),
                          0.0f,
                          0.0f,
                          texture.image.width,
                          texture.image.height};
}

void InitResources() {
    g.textures.clear();
    g.textures.resize(kFirstUserSlot);
    g.free_texture_slots.clear();
    g.meshes.clear();
    g.meshes.resize(1);
    g.free_mesh_slots.clear();

    // Slot 0 backs every draw without a texture so no sampler the shaders can reach is unbound.
    Texture &null = g.textures[0];
    null.image = CreateColorImage(1, 1, 1, false);
    null.desc.width = 1;
    null.desc.height = 1;
    ClearColorImage(null.image, 1.0f, 1.0f, 1.0f, 1.0f);
}

void ShutdownResources() {
    for (Texture &texture : g.textures) {
        DestroyImage(texture.image);
        DestroyImage(texture.depth);
    }
    for (Mesh &mesh : g.meshes) {
        if (mesh.vertex_buffer != 0) {
            gl.DeleteBuffers(1, &mesh.vertex_buffer);
        }
        if (mesh.index_buffer != 0) {
            gl.DeleteBuffers(1, &mesh.index_buffer);
        }
    }
    g.textures.clear();
    g.meshes.clear();
}

Texture *LookupTexture(TextureHandle handle) {
    uint32_t slot = handle & kSlotMask;
    if (slot < kFirstUserSlot || slot >= g.textures.size()) {
        return nullptr;
    }
    Texture &texture = g.textures[slot];
    if (!texture.live || texture.generation != handle >> kSlotBits || (texture.doomed && !ReplayingFlag())) {
        return nullptr;
    }
    return &texture;
}

Mesh *LookupMesh(MeshHandle handle) {
    uint32_t slot = handle & kSlotMask;
    if (slot == 0 || slot >= g.meshes.size()) {
        return nullptr;
    }
    Mesh &mesh = g.meshes[slot];
    if (!mesh.live || mesh.generation != handle >> kSlotBits || (mesh.doomed && !ReplayingFlag())) {
        return nullptr;
    }
    return &mesh;
}

Image *ColorImageOf(TextureHandle handle) {
    if (handle == kMainTarget) {
        return &MainColorTarget();
    }
    if (handle == kPreviousFrame) {
        return &PreviousFrameImage();
    }
    Texture *texture = LookupTexture(handle);
    return texture ? &texture->image : nullptr;
}

void RecreateRenderTargets() {
    for (uint32_t slot = kFirstUserSlot; slot < g.textures.size(); slot++) {
        Texture &texture = g.textures[slot];
        if (!texture.live || !texture.render_target || texture.shares_main_depth) {
            continue;
        }
        uint32_t width;
        uint32_t height;
        TargetPixelSize(texture, width, height);
        if (!CheckSize(width, height)) {
            width = texture.image.width;
            height = texture.image.height;
        }
        Texture resized = texture;
        resized.image.width = width;
        resized.image.height = height;
        LogicalMapping before = TextureMapping(texture);
        LogicalMapping after = TextureMapping(resized);
        bool           same_size = width == texture.image.width && height == texture.image.height;
        if (same_size && before.offset_x == after.offset_x && before.offset_y == after.offset_y) {
            continue;
        }
        Image old_image = texture.image;
        Image old_depth = texture.depth;
        texture.image = CreateTargetImage(width, height);
        texture.depth = CreateTargetDepth(width, height);
        // What lies outside the logical frame on either side is the frame's, not this image's.
        int   from[4];
        int   to[4];
        float w = static_cast<float>(texture.logical_width);
        float h = static_cast<float>(texture.logical_height);
        MappedRect(before, w, h, from);
        MappedRect(after, w, h, to);
        ClearColorImage(texture.image, 0.0f, 0.0f, 0.0f, 1.0f);
        if (from[2] > from[0] && from[3] > from[1] && to[2] > to[0] && to[3] > to[1]) {
            BlitImages(old_image, from[0], from[1], from[2], from[3], texture.image, to[0], to[1], to[2], to[3], true);
        }
        ClearDepthImage(texture.image, texture.depth);
        DestroyImage(old_image);
        DestroyImage(old_depth);
    }
}

void RecreateSharedTargets() {
    const Image &main = g.main_color[0];
    for (uint32_t slot = kFirstUserSlot; slot < g.textures.size(); slot++) {
        Texture &texture = g.textures[slot];
        if (!texture.live || !texture.shares_main_depth) {
            continue;
        }
        Image image = CreateTargetImage(main.width, main.height);
        ClearColorImage(image, 0.0f, 0.0f, 0.0f, 1.0f);
        Image old_image = texture.image;
        DestroyImage(old_image);
        texture.image = image;
    }
}

void DestroyDoomedTexture(TextureHandle handle) {
    uint32_t slot = handle & kSlotMask;
    if (slot < kFirstUserSlot || slot >= g.textures.size()) {
        return;
    }
    Texture &texture = g.textures[slot];
    if (!texture.live || texture.generation != handle >> kSlotBits) {
        return;
    }
    if (g.target == handle) {
        g.target = kMainTarget;
    }
    DestroyImage(texture.image);
    DestroyImage(texture.depth);
    texture.live = false;
    texture.doomed = false;
    texture.shadow.clear();
    texture.shadow.shrink_to_fit();
    g.free_texture_slots.push_back(slot);
}

void DestroyDoomedMesh(MeshHandle handle) {
    uint32_t slot = handle & kSlotMask;
    if (slot == 0 || slot >= g.meshes.size()) {
        return;
    }
    Mesh &mesh = g.meshes[slot];
    if (!mesh.live || mesh.generation != handle >> kSlotBits) {
        return;
    }
    if (g.bound.array_buffer == mesh.vertex_buffer) {
        g.bound.array_buffer = ~0u;
    }
    if (g.bound.element_buffer == mesh.index_buffer) {
        g.bound.element_buffer = ~0u;
    }
    gl.DeleteBuffers(1, &mesh.vertex_buffer);
    gl.DeleteBuffers(1, &mesh.index_buffer);
    mesh.vertex_buffer = 0;
    mesh.index_buffer = 0;
    mesh.live = false;
    mesh.doomed = false;
    g.free_mesh_slots.push_back(slot);
}

} // namespace detail

using namespace detail;

TextureHandle CreateTexture(const TextureDesc &desc) {
    ApiBarrier();
    if (!CheckSize(desc.width, desc.height)) {
        return kNullTexture;
    }
    uint32_t max_mips = std::bit_width(std::max(desc.width, desc.height));
    if (desc.mip_levels == 0 || desc.mip_levels > max_mips) {
        Error("texture %ux%u cannot have %u mip levels", desc.width, desc.height, desc.mip_levels);
        return kNullTexture;
    }
    uint32_t slot = AllocateTextureSlot();
    if (slot == 0) {
        Error("out of texture slots (%u)", kMaxTextures);
        return kNullTexture;
    }
    Texture &texture = g.textures[slot];
    texture.live = true;
    texture.generation = NextGeneration(texture.generation);
    texture.desc = desc;
    texture.render_target = false;
    texture.shares_main_depth = false;
    texture.frame = false;
    texture.logical_width = desc.logical_width != 0 ? desc.logical_width : desc.width;
    texture.logical_height = desc.logical_height != 0 ? desc.logical_height : desc.height;
    texture.doomed = false;
    bool index8 = desc.format == TextureFormat::Index8;
    texture.image = CreateColorImage(desc.width, desc.height, desc.mip_levels, index8);
    texture.shadow.clear();
    if (index8) {
        texture.shadow.assign(static_cast<size_t>(desc.width) * desc.height, 0);
    }
    return MakeHandle(texture.generation, slot);
}

TextureHandle CreatePalette() { return CreateTexture(TextureDesc{256, 1, TextureFormat::Rgba8, 1, true}); }

TextureHandle CreateRenderTarget(uint32_t logical_width, uint32_t logical_height, bool has_alpha,
                                 bool share_main_depth, bool frame_target) {
    ApiBarrier();
    if (share_main_depth) {
        logical_width = static_cast<uint32_t>(g.config.logical_width);
        logical_height = static_cast<uint32_t>(g.config.logical_height);
        frame_target = false;
    }
    Texture shape;
    shape.shares_main_depth = share_main_depth;
    shape.frame = frame_target;
    shape.logical_width = logical_width;
    shape.logical_height = logical_height;
    uint32_t width = 0;
    uint32_t height = 0;
    if (logical_width != 0 && logical_height != 0) {
        TargetPixelSize(shape, width, height);
    }
    if (logical_width == 0 || logical_height == 0 || !CheckSize(width, height)) {
        return kNullTexture;
    }
    uint32_t slot = AllocateTextureSlot();
    if (slot == 0) {
        Error("out of texture slots (%u)", kMaxTextures);
        return kNullTexture;
    }
    Texture &texture = g.textures[slot];
    texture.live = true;
    texture.generation = NextGeneration(texture.generation);
    texture.desc = TextureDesc{logical_width, logical_height, TextureFormat::Rgba8, 1, has_alpha};
    texture.render_target = true;
    texture.shares_main_depth = share_main_depth;
    texture.frame = frame_target;
    texture.logical_width = logical_width;
    texture.logical_height = logical_height;
    texture.doomed = false;
    texture.shadow.clear();
    texture.image = CreateTargetImage(width, height);
    ClearColorImage(texture.image, 0.0f, 0.0f, 0.0f, 1.0f);
    if (!share_main_depth) {
        texture.depth = CreateTargetDepth(width, height);
        ClearDepthImage(texture.image, texture.depth);
    }
    return MakeHandle(texture.generation, slot);
}

TextureHandle NamedRenderTarget(std::string_view name, uint32_t logical_width, uint32_t logical_height,
                                bool has_alpha, bool share_main_depth, bool frame_target) {
    TextureHandle existing = FindNamedRenderTarget(name);
    if (existing != kNullTexture) {
        const Texture &texture = *LookupTexture(existing);
        bool           same_size = share_main_depth || (texture.logical_width == logical_width &&
                                              texture.logical_height == logical_height);
        bool           same_shape = texture.shares_main_depth == share_main_depth &&
                          (share_main_depth || texture.frame == frame_target);
        if (same_size && same_shape && texture.desc.has_alpha == has_alpha) {
            return existing;
        }
        DestroyTexture(existing);
    }
    TextureHandle created = CreateRenderTarget(logical_width, logical_height, has_alpha, share_main_depth, frame_target);
    if (created != kNullTexture) {
        g.named_targets.emplace(std::string(name), created);
    }
    return created;
}

TextureHandle FindNamedRenderTarget(std::string_view name) {
    auto it = g.named_targets.find(name);
    return it == g.named_targets.end() ? kNullTexture : it->second;
}

void DestroyTexture(TextureHandle handle) {
    Texture *texture = LookupTexture(handle);
    if (texture == nullptr) {
        return;
    }
    std::erase_if(g.named_targets, [handle](const auto &entry) { return entry.second == handle; });
    if (RecordingCalls()) {
        if (g.record_target == handle) {
            SetRenderTarget(kMainTarget);
        }
        texture->doomed = true;
        RecordDoomedTexture(handle);
        return;
    }
    ApiBarrier();
    if (g.target == handle) {
        SetRenderTarget(kMainTarget);
    }
    DestroyDoomedTexture(handle);
}

namespace {

// Without GL_TEXTURE_MAX_LEVEL a mipmapped texture must hold its whole chain down to 1x1, where Vulkan
// stops at the last level the game uploads. When that last level arrives the rest is made from it
// (point-sampled for index textures, a 2x2 average for colour), so a distant surface reads the last
// real level as it would on Vulkan.
void FillLowerMips(Texture &texture, uint32_t from_level, const uint8_t *data) {
    bool                 index8 = texture.desc.format == TextureFormat::Index8;
    uint32_t             texel = index8 ? 1 : 4;
    uint32_t             w = std::max(1u, texture.image.width >> from_level);
    uint32_t             h = std::max(1u, texture.image.height >> from_level);
    uint32_t             full = static_cast<uint32_t>(std::bit_width(std::max(texture.image.width, texture.image.height)));
    std::vector<uint8_t> current(data, data + static_cast<size_t>(w) * h * texel);
    std::vector<uint8_t> next;
    GLenum               format = index8 ? GL_LUMINANCE : GL_RGBA;
    for (uint32_t level = from_level + 1; level < full; level++) {
        uint32_t nw = std::max(1u, w >> 1);
        uint32_t nh = std::max(1u, h >> 1);
        next.assign(static_cast<size_t>(nw) * nh * texel, 0);
        for (uint32_t y = 0; y < nh; y++) {
            for (uint32_t x = 0; x < nw; x++) {
                uint32_t sx = std::min(x * 2, w - 1);
                uint32_t sy = std::min(y * 2, h - 1);
                uint32_t sx1 = std::min(sx + 1, w - 1);
                uint32_t sy1 = std::min(sy + 1, h - 1);
                if (index8) {
                    next[static_cast<size_t>(y) * nw + x] = current[static_cast<size_t>(sy) * w + sx];
                    continue;
                }
                for (uint32_t c = 0; c < 4; c++) {
                    uint32_t sum = current[(static_cast<size_t>(sy) * w + sx) * 4 + c] +
                                   current[(static_cast<size_t>(sy) * w + sx1) * 4 + c] +
                                   current[(static_cast<size_t>(sy1) * w + sx) * 4 + c] +
                                   current[(static_cast<size_t>(sy1) * w + sx1) * 4 + c];
                    next[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
                }
            }
        }
        gl.TexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0, static_cast<GLsizei>(nw),
                         static_cast<GLsizei>(nh), format, GL_UNSIGNED_BYTE, next.data());
        current.swap(next);
        w = nw;
        h = nh;
    }
}

bool UpdateTextureImpl(TextureHandle handle, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       const void *pixels, uint32_t row_length, bool record) {
    if (!(record && RecordingCalls())) {
        ApiBarrier();
    }
    Texture *texture = LookupTexture(handle);
    if (texture == nullptr || pixels == nullptr) {
        Error("UpdateTexture: no texture %#x", handle);
        return false;
    }
    Image   &image = texture->image;
    uint32_t mip_width = std::max(1u, image.width >> mip);
    uint32_t mip_height = std::max(1u, image.height >> mip);
    if (mip >= image.mips || w == 0 || h == 0 || x + w > mip_width || y + h > mip_height) {
        Error("UpdateTexture: %ux%u at %u,%u is outside mip %u of %ux%u", w, h, x, y, mip, mip_width, mip_height);
        return false;
    }
    uint32_t texel = TexelSize(texture->desc.format);
    uint32_t stride = (row_length ? row_length : w) * texel;
    uint32_t row = w * texel;
    if (record && RecordingCalls()) {
        UpdateTextureEntry entry{handle, mip, x, y, w, h, std::vector<uint8_t>(static_cast<size_t>(row) * h)};
        for (uint32_t i = 0; i < h; i++) {
            std::memcpy(entry.pixels.data() + static_cast<size_t>(i) * row,
                        static_cast<const uint8_t *>(pixels) + static_cast<size_t>(i) * stride, row);
        }
        RecordEntry(std::move(entry));
        return true;
    }

    // ES 2 has no unpack row length: gather the rows when they are not already tight.
    std::vector<uint8_t> packed;
    const void          *data = pixels;
    if (stride != row) {
        packed.resize(static_cast<size_t>(row) * h);
        for (uint32_t i = 0; i < h; i++) {
            std::memcpy(packed.data() + static_cast<size_t>(i) * row,
                        static_cast<const uint8_t *>(pixels) + static_cast<size_t>(i) * stride, row);
        }
        data = packed.data();
    }
    if (texture->desc.format == TextureFormat::Index8 && mip == 0) {
        for (uint32_t i = 0; i < h; i++) {
            std::memcpy(texture->shadow.data() + static_cast<size_t>(y + i) * image.width + x,
                        static_cast<const uint8_t *>(data) + static_cast<size_t>(i) * row, row);
        }
    }
    if (texture->desc.format == TextureFormat::Index8 && std::getenv("GFX_TRACE") != nullptr) {
        std::fprintf(stderr, "upload idx handle=%#x gl=%u mip=%u at %u,%u %ux%u\n", handle, image.texture, mip, x, y, w, h);
    }
    BindTexture2D(0, image.texture);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLenum format = texture->desc.format == TextureFormat::Index8 ? GL_LUMINANCE : GL_RGBA;
    g_stats.uploads++;
    g_stats.upload_bytes += static_cast<uint64_t>(w) * h * (texture->desc.format == TextureFormat::Index8 ? 1 : 4);
    gl.TexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(mip), static_cast<GLint>(x), static_cast<GLint>(y),
                     static_cast<GLsizei>(w), static_cast<GLsizei>(h), format, GL_UNSIGNED_BYTE, data);
    if (!gl_caps.texture_max_level && image.mips > 1 && mip + 1 == image.mips && x == 0 && y == 0 &&
        w == mip_width && h == mip_height) {
        FillLowerMips(*texture, mip, static_cast<const uint8_t *>(data));
    }
    CheckGl("UpdateTexture");
    return true;
}

} // namespace

bool UpdateTexture(TextureHandle handle, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   const void *pixels, uint32_t row_length) {
    return UpdateTextureImpl(handle, mip, x, y, w, h, pixels, row_length, true);
}

bool UpdateTextureNow(TextureHandle handle, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const void *pixels, uint32_t row_length) {
    return UpdateTextureImpl(handle, mip, x, y, w, h, pixels, row_length, false);
}

bool UpdatePalette(TextureHandle palette, const uint32_t *rgba, uint32_t first, uint32_t count) {
    if (first + count > 256) {
        Error("UpdatePalette: entries %u..%u are past 256", first, first + count);
        return false;
    }
    return UpdateTexture(palette, 0, first, 0, count, 1, rgba);
}

std::optional<TextureInfo> GetTextureInfo(TextureHandle handle) {
    if (handle == kMainTarget || handle == kPreviousFrame) {
        const Image &image = *ColorImageOf(handle);
        return TextureInfo{static_cast<uint32_t>(g.config.logical_width),
                           static_cast<uint32_t>(g.config.logical_height),
                           image.width,
                           image.height,
                           TextureFormat::Rgba8,
                           1,
                           true,
                           true,
                           false,
                           false};
    }
    Texture *texture = LookupTexture(handle);
    if (texture == nullptr) {
        return std::nullopt;
    }
    return TextureInfo{texture->logical_width, texture->logical_height, texture->image.width,
                       texture->image.height, texture->desc.format, texture->desc.mip_levels,
                       texture->desc.has_alpha, texture->render_target, texture->shares_main_depth,
                       texture->frame};
}

void ConvertPs2Alpha(uint32_t *rgba, size_t count) {
    uint8_t *bytes = reinterpret_cast<uint8_t *>(rgba);
    for (size_t i = 0; i < count; i++) {
        uint8_t &alpha = bytes[i * 4 + 3];
        alpha = static_cast<uint8_t>(std::min(alpha * 2, 255));
    }
}

MeshHandle CreateMesh(std::span<const Vertex3D> vertices, std::span<const uint32_t> indices) {
    ApiBarrier();
    if (vertices.empty() || indices.empty()) {
        Error("CreateMesh: empty mesh");
        return kNullMesh;
    }
    bool index16 = !gl_caps.element_index_uint;
    if (index16 && vertices.size() > 65536) {
        Error("CreateMesh: %zu vertices need GL_OES_element_index_uint", vertices.size());
        return kNullMesh;
    }
    uint32_t slot;
    if (!g.free_mesh_slots.empty()) {
        slot = g.free_mesh_slots.back();
        g.free_mesh_slots.pop_back();
    } else if (g.meshes.size() < kMaxMeshes) {
        g.meshes.emplace_back();
        slot = static_cast<uint32_t>(g.meshes.size() - 1);
    } else {
        Error("out of mesh slots (%u)", kMaxMeshes);
        return kNullMesh;
    }
    Mesh &mesh = g.meshes[slot];
    mesh.live = true;
    mesh.doomed = false;
    mesh.generation = NextGeneration(mesh.generation);
    mesh.vertex_count = static_cast<uint32_t>(vertices.size());
    mesh.index_count = static_cast<uint32_t>(indices.size());
    mesh.index16 = index16;
    gl.GenBuffers(1, &mesh.vertex_buffer);
    gl.GenBuffers(1, &mesh.index_buffer);
    BindArrayBuffer(mesh.vertex_buffer);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), GL_STATIC_DRAW);
    BindElementBuffer(mesh.index_buffer);
    if (index16) {
        std::vector<uint16_t> narrow(indices.size());
        for (size_t i = 0; i < indices.size(); i++) {
            narrow[i] = static_cast<uint16_t>(indices[i]);
        }
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(narrow.size() * sizeof(uint16_t)),
                      narrow.data(), GL_STATIC_DRAW);
    } else {
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(),
                      GL_STATIC_DRAW);
    }
    CheckGl("CreateMesh");
    return MakeHandle(mesh.generation, slot);
}

bool UpdateMeshVertices(MeshHandle handle, uint32_t first, std::span<const Vertex3D> vertices) {
    Mesh *mesh = LookupMesh(handle);
    if (mesh == nullptr || first + vertices.size() > mesh->vertex_count) {
        Error("UpdateMeshVertices: bad mesh %#x or range", handle);
        return false;
    }
    if (vertices.empty()) {
        return true;
    }
    if (RecordingCalls()) {
        RecordEntry(UpdateMeshEntry{handle, first, std::vector<Vertex3D>(vertices.begin(), vertices.end())});
        return true;
    }
    ApiBarrier();
    BindArrayBuffer(mesh->vertex_buffer);
    gl.BufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(first * sizeof(Vertex3D)),
                     static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data());
    return true;
}

void DestroyMesh(MeshHandle handle) {
    Mesh *mesh = LookupMesh(handle);
    if (mesh == nullptr) {
        return;
    }
    if (RecordingCalls()) {
        mesh->doomed = true;
        RecordDoomedMesh(handle);
        return;
    }
    ApiBarrier();
    DestroyDoomedMesh(handle);
}

} // namespace gfx
