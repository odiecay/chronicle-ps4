#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../gfx/displaylist.hpp"
#include "context.hpp"

namespace gfx {

namespace detail {

Context    g;
FrameStats g_stats;
Tune       g_tune;

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Fatal(const char *format, ...) {
    std::va_list args;
    va_start(args, format);
    std::fputs("gfx: ", stderr);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
    va_end(args);
    std::exit(1);
}

void Error(const char *format, ...) {
    if (g.quiet) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    std::fputs("gfx: ", stderr);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
    va_end(args);
}

void CheckGl(const char *where) {
    if (!g.gl_debug) {
        return;
    }
    for (int i = 0; i < 8; i++) {
        GLenum error = gl.GetError();
        if (error == GL_NO_ERROR) {
            return;
        }
        std::fprintf(stderr, "gfx: %s: %s\n", where, GlErrorName(error));
    }
}

namespace {

// The window's pixel size, or false when there is nothing to draw to (minimised).
bool WindowExtent(uint32_t &width, uint32_t &height) {
    int w = 0;
    int h = 0;
    HostWindowSize(w, h);
    if (w <= 0 || h <= 0) {
        return false;
    }
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
    return true;
}

int SwapInterval() { return g.config.present_mode == PresentMode::Fifo ? 1 : 0; }

// The main targets follow the window's size; render targets sized from it follow them.
void SyncMainTargets() {
    uint32_t width;
    uint32_t height;
    g.resize_pending = false;
    if (!WindowExtent(width, height)) {
        return;
    }
    if (width == g.extent_width && height == g.extent_height && g.main_color[0].texture != 0) {
        return;
    }
    ApiBarrier();
    g.extent_width = width;
    g.extent_height = height;
    DestroyMainTargets();
    CreateMainTargets();
    RecreateSharedTargets();
    RecreateRenderTargets();
}

void FinishFrame() { g.main_current ^= 1; }

void DropDepthQueries() {
    // ES 2 cannot read the depth buffer back, so a pick has no answer; the game takes that as "no hit".
    for (DepthQuery &query : g.depth_queries) {
        query.queued = false;
        query.recorded = false;
        query.valid = false;
    }
}

bool OpenFrame() {
    g.in_frame = true;
    g.frame_serial++;
    g.target = kMainTarget;
    ResetDrawState();
    return true;
}

void SwapNow() { HostSwap(SwapInterval()); }

} // namespace

Image &CurrentMainColor() { return g.main_color[g.main_current]; }

Image &PreviousMainColor() { return g.main_color[g.main_current ^ 1]; }

void CreateMainTargets() {
    uint32_t width = g.extent_width;
    uint32_t height = g.extent_height;
    for (Image &image : g.main_color) {
        image = CreateColorImage(width, height, 1, false, false);
        ClearColorImage(image, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    g.main_depth = CreateSceneDepth(g.main_color[0], width, height);
    ClearDepthImage(g.main_color[0], g.main_depth);
    ClearDepthImage(g.main_color[1], g.main_depth);
}

// The display render's image and depth are made the first time a display render needs them: with interpolation
// off they never are, which leaves about 16 MiB of Piglet's small pool to the game's own targets at 1080p.
void EnsureDisplayTargets() {
    if (g.display_color.texture != 0) {
        return;
    }
    g.display_color = CreateColorImage(g.extent_width, g.extent_height, 1, false, false);
    ClearColorImage(g.display_color, 0.0f, 0.0f, 0.0f, 1.0f);
    g.display_depth = CreateSceneDepth(g.display_color, g.extent_width, g.extent_height);
    ClearDepthImage(g.display_color, g.display_depth);
}

void DestroyMainTargets() {
    for (Image &image : g.main_color) {
        DestroyImage(image);
    }
    DestroyImage(g.main_depth);
    DestroyImage(g.display_color);
    DestroyImage(g.display_depth);
}

Image &MainColorTarget() { return g.display_pass ? g.display_color : CurrentMainColor(); }

Image &MainDepth() { return g.display_pass ? g.display_depth : g.main_depth; }

// A display render of the newest list samples what its canonical render sampled, which the flip at
// the end of that render left as the current image.
Image &PreviousFrameImage() { return g.display_pass ? CurrentMainColor() : PreviousMainColor(); }

void PrepareRecording() {
    if (g.resize_pending) {
        SyncMainTargets();
    } else if (!g.offscreen) {
        SyncMainTargets();
    }
}

bool OpenListFrame(bool canonical, bool present, bool base) {
    if (canonical) {
        // The game's state is drawn whether or not the window can show it: later ticks sample it.
        SyncMainTargets();
        return OpenFrame();
    }
    if (present && !g.offscreen) {
        uint32_t width;
        uint32_t height;
        if (!WindowExtent(width, height)) {
            return false;
        }
        if (width != g.extent_width || height != g.extent_height) {
            // A resize between the tick and its display render: the next tick rebuilds the targets.
            return false;
        }
    }
    EnsureDisplayTargets();
    OpenFrame();
    g.display_pass = true;
    if (base) {
        Image &source = PreviousMainColor();
        BlitImages(source, 0, 0, static_cast<int>(source.width), static_cast<int>(source.height), g.display_color, 0,
                   0, static_cast<int>(g.display_color.width), static_cast<int>(g.display_color.height), false);
    }
    return true;
}

void CloseListFrame(bool canonical, bool present) {
    EndRendering();
    ReleaseTarget();
    if (canonical) {
        DropDepthQueries();
        g.in_frame = false;
        FinishFrame();
        g.canonical_newest = true;
        g.presented_display = false;
        return;
    }
    if (present && !g.offscreen) {
        PresentImage(g.display_color);
        SwapNow();
    }
    g.in_frame = false;
    g.display_pass = false;
    g.target = kMainTarget;
    if (present) {
        g.presented_display = true;
    }
}

} // namespace detail

using namespace detail;

float LogicalWidth() { return g.config.logical_width; }

float LogicalHeight() { return g.config.logical_height; }

LogicalRect LogicalFrame() { return {0.0f, 0.0f, LogicalWidth(), LogicalHeight()}; }

void RendererInit(SDL_Window *window, const RendererConfig &config) {
    static uint64_t instances = 0;
    g = {};
    g.renderer_instance = ++instances;
    g.window = window;
    g.config = config;
    g.offscreen = config.offscreen;
    g.layout = config.layout;
    g.gl_debug = std::getenv("GFX_GL_DEBUG") != nullptr;

    if (!HostCreateContext(window, g.offscreen)) {
        Fatal("cannot create an OpenGL ES 2 context");
    }
    const char *missing = nullptr;
    if (!GlLoad(HostGetProc, &missing)) {
        Fatal("the OpenGL ES driver lacks %s", missing);
    }
    std::fprintf(stderr, "gfx: OpenGL ES backend on %s (%s)\n",
                 reinterpret_cast<const char *>(gl.GetString(GL_RENDERER)),
                 reinterpret_cast<const char *>(gl.GetString(GL_VERSION)));

    uint32_t width = 1;
    uint32_t height = 1;
    if (!WindowExtent(width, height)) {
        width = 1;
        height = 1;
        g.resize_pending = true;
    }
    g.extent_width = width;
    g.extent_height = height;

    gl.GenBuffers(1, &g.stream_vertices);
    gl.GenBuffers(1, &g.stream_indices);
    gl.GenBuffers(1, &g.quad_buffer);
    static const float kCorners[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    BindArrayBuffer(g.quad_buffer);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(kCorners), kCorners, GL_STATIC_DRAW);

    BuildBlendTable();
    InitResources();
    InitBlit();
    gl.UseProgram(g.blit_program);
    g.bound.program = g.blit_program;
    gl.Uniform1i(g.blit_tex, 0);
    CreateMainTargets();
    g.render_scale = config.render_scale > 0.0f
                         ? config.render_scale
                         : std::max(1.0f, std::round(static_cast<float>(g.extent_height) / g.config.logical_height));

    // Compile every program now, so no draw stalls on one later.
    auto     start = std::chrono::steady_clock::now();
    uint32_t total = 2 * kTextureModeCount * 2;
    uint32_t done = 0;
    for (PipelineFamily family : {kFamily2DTriangles, kFamilyMesh}) {
        for (uint32_t mode = 0; mode < kTextureModeCount; mode++) {
            for (bool alpha_test : {false, true}) {
                GetProgram(family, static_cast<TextureMode>(mode), alpha_test);
                if (config.progress) {
                    config.progress(++done, total);
                }
            }
        }
    }
    (void)start;
    g.ready = true;
    ResetDrawState();
    CheckGl("RendererInit");
    RenderThreadStart();
}

void RendererShutdown() {
    if (!g.ready) {
        return;
    }
    RenderThreadStop();
    g.list.reset();
    ShutdownResources();
    DestroyMainTargets();
    ShutdownPrograms();
    gl.DeleteBuffers(1, &g.stream_vertices);
    gl.DeleteBuffers(1, &g.stream_indices);
    gl.DeleteBuffers(1, &g.quad_buffer);
    for (auto &[key, fbo] : g.framebuffers) {
        gl.DeleteFramebuffers(1, &fbo);
    }
    HostDestroyContext();
    uint32_t messages = g.validation_messages;
    g = {};
    g.validation_messages = messages;
}

void RendererResize() { g.resize_pending = true; }

void SetAnisotropy(int samples) {
    uint32_t level = 0;
    while (level < kAnisotropyLevels && (2u << level) <= static_cast<uint32_t>(samples)) {
        ++level;
    }
    g.anisotropy_level = level;
}

void SetPresentMode(PresentMode mode) { g.config.present_mode = mode; }

bool BeginFrame() {
    ApiBarrier();
    g_canonical_submitted = false;
    if (g.in_frame || RecordingCalls()) {
        Error("BeginFrame inside a frame");
        return true;
    }
    uint32_t width;
    uint32_t height;
    if (!WindowExtent(width, height)) {
        return false;
    }
    if (width != g.extent_width || height != g.extent_height) {
        SyncMainTargets();
    }
    return OpenFrame();
}

void EndFrame() {
    ApiBarrier();
    g_canonical_submitted = false;
    if (!g.in_frame) {
        return;
    }
    EndRendering();
    ReleaseTarget();
    DropDepthQueries();
    g.canonical_newest = false;
    g.presented_display = false;
    if (!g.offscreen) {
        PresentImage(CurrentMainColor());
        SwapNow();
    }
    g.in_frame = false;
    FinishFrame();
}

bool PresentCanonical() {
    if (g_threaded && !OnRenderThread()) {
        if (!g_canonical_submitted) {
            return false;
        }
        RenderSubmit([] { PresentCanonical(); });
        return true;
    }
    if (!g.canonical_newest || g.in_frame || RecordingCalls()) {
        return false;
    }
    g.presented_display = false;
    if (g.offscreen) {
        return true;
    }
    uint32_t width;
    uint32_t height;
    if (!WindowExtent(width, height)) {
        return false;
    }
    if (width != g.extent_width || height != g.extent_height) {
        return false;
    }
    PresentImage(PreviousMainColor());
    SwapNow();
    return true;
}

bool InFrame() { return (g.in_frame && !g_threaded) || RecordingCalls(); }

RendererFeatures ActiveRendererFeatures() {
    // The ES 2 backend always has fans and separate stencil state, and colour masks.
    return {0x00020000u, g.offscreen, true, true, true, false};
}

bool HeadlessSurfaceAvailable() { return true; }

uint32_t ValidationMessageCount() { return g.validation_messages; }

uint32_t PipelineCount() { return static_cast<uint32_t>(g.programs.size()); }

double PipelineCompileSeconds() { return 0.0; }

float RenderScale() { return g.render_scale; }

void SetFrameLayout(const FrameLayout &layout) {
    ApiBarrier();
    if (g.in_frame || RecordingCalls()) {
        Error("SetFrameLayout inside a frame");
        return;
    }
    if (!(layout.ui_scale > 0.0f) || !std::isfinite(layout.ui_scale)) {
        Error("SetFrameLayout: ui_scale %g", static_cast<double>(layout.ui_scale));
        return;
    }
    g.layout = layout;
    if (g.ready) {
        RecreateRenderTargets();
    }
}

FrameLayout CurrentFrameLayout() { return g.layout; }

void SetRenderScale(float scale) {
    ApiBarrier();
    if (scale <= 0.0f || scale == g.render_scale || g.in_frame || RecordingCalls()) {
        return;
    }
    g.render_scale = scale;
    RecreateRenderTargets();
}

} // namespace gfx
