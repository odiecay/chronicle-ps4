#pragma once

#include <functional>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/gfx.hpp"
#include "gl.hpp"

// The OpenGL ES 2.0 backend of gfx.hpp. It mirrors the Vulkan renderer's structure (same Context
// fields where displaylist.cpp reads them) but draws immediately: there are no command buffers,
// descriptor sets or barriers. See README.md in this directory for the conventions it keeps.

namespace gfx::detail {

inline constexpr uint32_t kMaxTextures = 8192;
inline constexpr uint32_t kMaxMeshes = 65535;
inline constexpr uint32_t kFirstUserSlot = 3;
inline constexpr uint32_t kAnisotropyLevels = 4;

[[noreturn]] void Fatal(const char *format, ...) __attribute__((format(printf, 1, 2)));
void              Error(const char *format, ...) __attribute__((format(printf, 1, 2)));
// Reports a pending GL error under a label; only active while GFX_GL_DEBUG is set.
void CheckGl(const char *where);

// ---- Images ----------------------------------------------------------------------------------

// A colour image is a GL texture; a depth image is a pair of renderbuffers (or one packed).
struct Image {
    GLuint   texture = 0;
    GLuint   depth_rb = 0;
    GLuint   stencil_rb = 0; // separate from depth_rb only without GL_OES_packed_depth_stencil
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mips = 1;
    bool     index8 = false;
    bool     has_stencil = false;
    int      depth_bits = 0; // what the driver gave the depth renderbuffer
    // Sampler state last set on the texture object, so a draw sets only what changes.
    GLint min_filter = -1;
    GLint mag_filter = -1;
    GLint wrap_s = -1;
    GLint wrap_t = -1;
};

struct Texture {
    bool        live = false;
    uint16_t    generation = 0;
    TextureDesc desc;
    Image       image;
    Image       depth; // render targets that keep their own only
    bool        render_target = false;
    bool        shares_main_depth = false;
    bool        frame = false;
    uint32_t    logical_width = 0;
    uint32_t    logical_height = 0;
    // Destroyed while a list that may draw it was recorded: dead to the API, alive to replays.
    bool doomed = false;
    // Index8 textures cannot be read back from the GPU in ES 2, so copies between them go through this.
    std::vector<uint8_t> shadow;
};

struct Mesh {
    bool     live = false;
    uint16_t generation = 0;
    GLuint   vertex_buffer = 0;
    GLuint   index_buffer = 0;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    bool     index16 = false;
    bool     doomed = false;
};

struct DepthQuery {
    LogicalRect rect = {};
    bool        queued = false;
    bool        recorded = false;
    bool        valid = false;
    float       result = 0.0f;
};

struct BlendMapping {
    uint16_t slot;      // index into the blend equations
    uint8_t  transform; // what the shader writes as source colour, kSource*
    bool     fix;       // C is FIX: the factor is the constant FIX / 128
};

enum SourceTransform : uint8_t {
    kSourceColor = 0,
    kSourceTimesOnePlusC = 1,
    kSourceFactor = 2,
};

// One GL blend configuration for a GS blend equation.
struct BlendEquation {
    GLenum src;
    GLenum dst;
    GLenum op;
    bool   constant_alpha; // a factor reads the constant colour's alpha (GS FIX)
};

// Axes along which an image shows past its logical frame.
enum ExtentAxis : uint32_t {
    kAxisX = 1u << 0,
    kAxisY = 1u << 1,
};

enum PipelineFamily : uint32_t {
    kFamily2DTriangles,
    kFamily2DLines,
    kFamilyMesh,
};

enum TextureMode : uint32_t {
    kTextureNone,
    kTextureRgba,
    kTexturePalette,
    kTextureModeCount,
};

// The values last sent to one program's uniforms: a program keeps them, so a draw sends only what changed.
struct UniformCache {
    bool  valid = false;
    float xform[4] = {};
    float uv_xform[4] = {};
    float tex_size[2] = {};
    float fog_color[3] = {};
    float scalars[11] = {};
    // The mesh block, by group (see SetMeshUniforms).
    bool  mesh_valid = false;
    float mvp[16] = {};
    float normal[9] = {};
    float lights[32] = {};
    float material[16] = {};
    float clip[16] = {};
    float flags[9] = {};
};

// Where a draw's data sits in the frame's batch buffers (see BeginImmediateBatch).
struct BatchSlot {
    size_t   vertex_offset = 0; // bytes into batch_vertices
    size_t   index_offset = 0;  // bytes into batch_indices (meshes)
    uint32_t count = 0;         // 2D: vertices drawn (quads already split into triangles)
};

// What one linked program needs set per draw.
struct Program {
    mutable UniformCache cache;
    GLuint id = 0;
    GLint  xform = -1, uv_xform = -1, tex = -1, pal = -1, tex_size = -1;
    GLint  fog = -1, texa = -1, aem = -1, opaque_target = -1, transform = -1, factor_fix = -1, linear = -1;
    GLint  alpha_ref = -1, alpha_func = -1, fix = -1, ta0 = -1, fog_color = -1;
    // Mesh programs only.
    GLint mvp = -1, normal_matrix = -1, light_direction = -1, light_color = -1, ambient = -1, diffuse = -1;
    GLint ambient_material = -1, fog_params = -1, clip_plane = -1, world_uv = -1;
    GLint flag_lit = -1, flag_vertex_color = -1, flag_fog = -1, flag_shadow = -1, flag_clip0 = -1;
    GLint flag_clip1 = -1, flag_screen_uv = -1, flag_world_uv = -1, light_count = -1;
};

// GL state the draw path has set, so unchanged state costs no call. -1 is unknown.
struct BoundState {
    GLuint  program = ~0u;
    GLuint  fbo = ~0u;
    GLuint  array_buffer = ~0u;
    GLuint  element_buffer = ~0u;
    int     blend = -1;
    int     blend_slot = -1;
    int     depth_test = -1;
    int     depth_write = -1;
    int     depth_op = -1;
    int     stencil_test = -1;
    int     cull = -1;
    int     color_mask = -1;
    int     scissor_test = -1;
    int     scissor[4] = {-1, -1, -1, -1};
    int     viewport[4] = {-1, -1, -1, -1};
    GLuint  texture[2] = {~0u, ~0u};
    int     active_unit = -1;
    int     stencil_ops[2] = {-1, -1};
    int     stencil_func[2] = {-1, -1};
    int     stencil_mask[2] = {-1, -1};
    int     const_alpha = -1;
    int     vertex_layout = -1;
};

struct Context {
    SDL_Window *window = nullptr;
    RendererConfig config;
    bool           ready = false;
    bool           offscreen = false;
    bool           resize_pending = false;
    uint32_t       extent_width = 1;
    uint32_t       extent_height = 1;
    bool           depth_has_stencil = true;

    bool     in_frame = false;
    uint64_t frame_serial = 1;

    // Two main colour images, swapped every frame: one is drawn, the other is the previous frame.
    std::array<Image, 2> main_color;
    Image                main_depth;
    uint32_t             main_current = 0;
    float                render_scale = 1.0f;
    FrameLayout          layout;
    UiAnchor             ui_anchor;

    std::vector<Texture>                              textures;
    std::map<std::string, TextureHandle, std::less<>> named_targets;
    std::vector<uint32_t>                             free_texture_slots;
    std::vector<Mesh>                                 meshes;
    std::vector<uint32_t>                             free_mesh_slots;

    std::vector<BlendEquation>           equations;
    std::array<BlendMapping, 81>         blend_map = {};
    std::unordered_map<uint32_t, Program> programs;
    GLuint                               blit_program = 0;
    GLint                                blit_rect = -1;
    GLint                                blit_uv = -1;
    GLint                                blit_tex = -1;
    GLuint                               stream_vertices = 0;
    GLuint                               stream_indices = 0;
    // Where the next streamed draw's data goes in each buffer; capacities in bytes (0: not allocated yet).
    size_t                               stream_vertex_cursor = 0;
    size_t                               stream_vertex_capacity = 0;
    size_t                               stream_index_cursor = 0;
    size_t                               stream_index_capacity = 0;
    GLuint                               quad_buffer = 0;
    // One upload a frame for every immediate mesh and 2D draw of the list being replayed, instead of two
    // glBufferData calls per draw (each one allocates in Piglet, about 35 us a draw on the console).
    GLuint                                       batch_vertices = 0;
    GLuint                                       batch_indices = 0;
    std::unordered_map<const void *, BatchSlot>  batch_slots;
    std::unordered_map<uint64_t, GLuint> framebuffers;
    BoundState                           bound;
    uint32_t                             anisotropy_level = 0;

    TextureHandle target = kMainTarget;

    std::array<DepthQuery, kDepthQueryCount> depth_queries;

    // Display lists. The canonical render of a list draws into main_color like an immediate frame;
    // a display render draws into display_color with display_depth.
    uint64_t                     renderer_instance = 0;
    uint64_t                     list_serial = 0;
    std::shared_ptr<DisplayList> list;
    TextureHandle                record_target = kMainTarget;
    InterpKey                    interp_key = 0;
    bool                         interp_no_interpolation = false;
    float                        interp_teleport_distance = INFINITY;
    bool                         interp_blend_vertices = false;
    bool                         replaying = false;
    bool                         display_pass = false;
    // Errors are reported by the canonical render; display renders of the same list stay quiet.
    bool  quiet = false;
    // The list being replayed is the host's own 2D, which the full-frame rule leaves as drawn.
    bool  host_draws = false;
    Image display_color;
    Image display_depth;
    // The newest main_color image is a canonical render no immediate frame has followed.
    bool canonical_newest = false;
    bool presented_display = false;
    // The image the last present drew to the window, for the platform's screenshot.
    const Image *last_presented = nullptr;

    uint32_t validation_messages = 0;
    bool     gl_debug = false;
};

extern Context g;

// Switches for finding out what the GPU time goes on, set live by the platform from a file (host_ps4.cpp reads
// /data/darkcloud/ps4_tune): each takes something out of the frame so the change in time shows what it cost.
struct Tune {
    int  draw_limit = -1;   // drop every draw after the Nth of a frame
    int  viewport_div = 1;  // draw into a viewport this many times smaller: pixel work shrinks, vertex work does not
    bool nearest = false;   // never filter textures in the shader
    bool no_blend = false;
    bool skip_2d = false;
    bool skip_mesh = false;
    bool no_batch = false;      // draw every immediate mesh and 2D draw with its own upload, as before batching
    bool client_arrays = false; // vertex and index data from client memory, with no buffer object per draw
    bool timing = false;        // time the phases of a draw (FrameStats ms_ fields)
};
extern Tune g_tune;

double NowMs();
// Adds the time its scope took to a FrameStats field when g_tune.timing is on.
struct PhaseTimer {
    double &accumulator;
    double  start;
    explicit PhaseTimer(double &a) : accumulator(a), start(g_tune.timing ? NowMs() : 0.0) {}
    ~PhaseTimer() {
        if (g_tune.timing) {
            accumulator += NowMs() - start;
        }
    }
};

// What a frame cost, for the platform's log (ps4/src/host_ps4.cpp prints and clears it).
struct FrameStats {
    uint32_t draws = 0;
    uint32_t blits = 0;
    uint32_t uploads = 0;
    uint64_t upload_bytes = 0;
    uint32_t fbo_binds = 0;
    uint32_t programs = 0;
    uint32_t n_2d = 0;
    uint32_t n_immediate = 0;
    uint32_t n_retained = 0;
    double   ms_prepare = 0.0; // Prepare: program, textures, state, per-draw uniforms
    double   ms_data = 0.0;    // mesh uniforms, vertex data and attribute setup
    double   ms_issue = 0.0;   // the draw call itself
    double   ms_batch = 0.0;   // gathering and uploading the frame batch
    double   ms_target = 0.0;  // render target changes, clears, copies and blits
    double   ms_texture = 0.0; // texture and mesh data uploads
    uint32_t stream_orphans = 0;
    uint64_t stream_bytes = 0;
};
extern FrameStats g_stats;

// ---- Host (SDL on the PC, the PS4 platform layer there) ---------------------------------------

// Creates an ES 2 context for the window and makes it current; false when none can be had.
// ---- The render thread (renderthread.cpp) ---------------------------------------------------------

// (Not thread_local: the console's FSELF builder cannot resolve the TLS init wrappers a cross-unit extern
// thread_local needs, so the render thread is told apart by its id.)
bool OnRenderThread();
extern bool g_replaying_game;
extern bool g_replaying_render;
inline bool &ReplayingFlag() { return OnRenderThread() ? g_replaying_render : g_replaying_game; }
extern bool              g_threaded;       // the render thread is running
extern bool              g_canonical_submitted;
bool RenderThreadStart();
void RenderThreadStop();
// Queues a job for the render thread; waits while the previous tick's jobs are still running.
void RenderSubmit(std::function<void()> job);
// Calls that are not just recording first wait for the render thread to finish and take the GL context back.
void ApiBarrier();
bool HostMakeCurrent(bool on);
bool HostRenderThreadWanted();

// list is a const gfx::DisplayList *.
void BeginImmediateBatch(const void *list);
void EndImmediateBatch();

bool  HostCreateContext(SDL_Window *window, bool offscreen);
void  HostDestroyContext();
void *HostGetProc(const char *name);
// Presents the default framebuffer; interval is the swap interval (0 immediate, 1 vsync).
void HostSwap(int interval);
void HostWindowSize(int &width, int &height);

// ---- renderer.cpp ------------------------------------------------------------------------------

bool OpenListFrame(bool canonical, bool present, bool base);
void PrepareRecording();
void CloseListFrame(bool canonical, bool present);
Image &CurrentMainColor();
Image &PreviousMainColor();
Image &MainColorTarget();
Image &MainDepth();
Image &PreviousFrameImage();
void   CreateMainTargets();
void   DestroyMainTargets();
// Runs now: GL has no deferred destruction to wait for.
inline void DeferDestroy(const std::function<void()> &destroy) { destroy(); }

// ---- images.cpp (resources) -------------------------------------------------------------------

Image  CreateColorImage(uint32_t width, uint32_t height, uint32_t mips, bool index8, bool zero = true);
// `want_bits` 24 or 32 asks for that renderbuffer format (without a packed depth-stencil format); 0 is the default.
Image  CreateDepthImage(uint32_t width, uint32_t height, int want_bits = 0);
// The scene depth, which the shadow volumes are counted against: the best of 24 and 32 bits that the driver gives
// and that makes a complete framebuffer with `color`, else the default. A driver can list a format and then give
// nothing for it (the PS4's Piglet gives 32 bits no storage), so each is tried on a real framebuffer.
Image  CreateSceneDepth(const Image &color, uint32_t width, uint32_t height);
void   DestroyImage(Image &image);
void   ClearColorImage(Image &image, float r, float g_, float b, float a);
void   ClearDepthImage(Image &color, Image &depth);
GLuint FramebufferFor(const Image &color, const Image *depth);
void   BindFbo(GLuint fbo);
void   BindArrayBuffer(GLuint buffer);
void   BindElementBuffer(GLuint buffer);
void   ForgetFramebuffers(const Image &image);
void   BindTexture2D(uint32_t unit, GLuint texture);
// Applies filter and wrap to the texture bound on the unit when they differ from what it has.
void SetSampler(uint32_t unit, Image &image, bool linear, Wrap wrap_u, Wrap wrap_v);

Texture *LookupTexture(TextureHandle handle);
Mesh    *LookupMesh(MeshHandle mesh);
Image   *ColorImageOf(TextureHandle handle);
void     InitResources();
void     ShutdownResources();
void     RecreateRenderTargets();
void     RecreateSharedTargets();
LogicalMapping TextureMapping(const Texture &texture);
bool           FrameShaped(const Texture &texture);
void           DestroyDoomedTexture(TextureHandle handle);
void           DestroyDoomedMesh(MeshHandle handle);

// ---- shaders.cpp ------------------------------------------------------------------------------

void           BuildBlendTable();
BlendMapping   MapBlend(const GsBlend &blend, bool enable);
const Program *GetProgram(PipelineFamily family, TextureMode mode, bool alpha_test);
bool           InitBlit();
void           ShutdownPrograms();

// ---- displaylist.cpp ----------------------------------------------------------------------------

bool RecordingCalls();

// ---- draw.cpp ----------------------------------------------------------------------------------

void           EndRendering();
void           ResetDrawState();
void           ReleaseTarget();
LogicalMapping MainMapping(uint32_t width, uint32_t height);
LogicalMapping UiMapping(const LogicalMapping &mapping);
bool           FillsWindow();
uint32_t       ExtentAxes(TextureHandle handle);
LogicalRect    LogicalFrame(TextureHandle handle);
LogicalRect    TargetBounds(const LogicalMapping &mapping);
LogicalRect    ExtendRect(LogicalRect rect, uint32_t axes, const LogicalRect &frame, const LogicalMapping &mapping);
// Draws src's rect (pixels, a negative extent mirrors) over dst's rect, through the blit program.
void BlitImages(Image &src, int sx0, int sy0, int sx1, int sy1, Image &dst, int dx0, int dy0, int dx1, int dy1,
                bool linear);
// Draws a colour image over the window's default framebuffer, the right way up.
void PresentImage(Image &source);

// Writes the last presented picture as a PNG; false when nothing was presented.
bool SaveLastPresented(const char *path);

// ---- readback.cpp -----------------------------------------------------------------------------

void ReadImagePixels(const Image &image, std::vector<uint8_t> &pixels);

} // namespace gfx::detail
