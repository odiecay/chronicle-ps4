// The GL host of the ES 2 renderer on the PS4: Piglet's EGL context (gfx_gles/host_sdl.cpp is the SDL one).

#ifdef __PS4__

// Piglet exports the core ES 2 entry points as plain symbols (and eglGetProcAddress does not return them), so the
// renderer is handed those. Pigletv2VSH.h declares them and has to come before the renderer's own GL header.
#include <orbis/Pigletv2VSH.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "context.hpp"
#include "ps4_platform.hpp"

// libkernel's clock call; its own clock ids are FreeBSD's.
extern "C" int sceKernelClockGettime(int clock_id, struct timespec *tp);

namespace ps4 {
void DisplaySize(int &width, int &height);
}

namespace gfx::detail {

namespace {
bool g_first_frame = true;
}

bool HostCreateContext(SDL_Window *, bool) {
    int width;
    int height;
    ps4::DisplaySize(width, height);
    return ps4::InitGraphics(width, height);
}

void HostDestroyContext() { ps4::ShutdownGraphics(); }

bool HostMakeCurrent(bool on) { return ps4::MakeCurrent(on); }

// The replay of each tick runs on its own thread (57 to 61 ticks a second in the heaviest scene tried, against 28 to
// 38 without), so a new install must get it with no setup. An empty file /data/darkcloud/ps4_no_render_thread turns
// it off; a self-test at start-up falls back to one thread if the console refuses the hand-over.
bool HostRenderThreadWanted() { return access("/data/darkcloud/ps4_no_render_thread", F_OK) != 0; }

void *HostGetProc(const char *name) {
    static const struct {
        const char *name;
        void       *address;
    } kCore[] = {
#define X(function, type) {"gl" #function, reinterpret_cast<void *>(&gl##function)},
        GFX_GL_FUNCTIONS(X)
#undef X
    };
    for (const auto &entry : kCore) {
        if (strcmp(entry.name, name) == 0) {
            return entry.address;
        }
    }
    return ps4::GetProc(name);
}

// Reads /data/darkcloud/ps4_tune ("key=value" lines: draw_limit, viewport_div, nearest, no_blend, skip_2d,
// skip_mesh) every 30 frames; a missing file or key means the normal frame.
void ReadTune() {
    Tune tune;
    FILE *file = fopen("/data/darkcloud/ps4_tune", "r");
    if (file != nullptr) {
        char line[80];
        while (fgets(line, sizeof(line), file) != nullptr) {
            char key[40];
            int  value = 0;
            if (sscanf(line, "%39[a-z_0-9]=%d", key, &value) == 2) {
                if (strcmp(key, "draw_limit") == 0) tune.draw_limit = value;
                if (strcmp(key, "viewport_div") == 0 && value >= 1 && value <= 8) tune.viewport_div = value;
                if (strcmp(key, "nearest") == 0) tune.nearest = value != 0;
                if (strcmp(key, "client_arrays") == 0) tune.client_arrays = value != 0;
                if (strcmp(key, "timing") == 0) tune.timing = value != 0;
                if (strcmp(key, "no_batch") == 0) tune.no_batch = value != 0;
                if (strcmp(key, "no_blend") == 0) tune.no_blend = value != 0;
                if (strcmp(key, "skip_2d") == 0) tune.skip_2d = value != 0;
                if (strcmp(key, "skip_mesh") == 0) tune.skip_mesh = value != 0;
            }
        }
        fclose(file);
    }
    if (memcmp(&tune, &g_tune, sizeof(Tune)) != 0) {
        g_tune = tune;
        ps4::Log("[PS4] tune: draw_limit %d viewport_div %d nearest %d no_blend %d skip_2d %d skip_mesh %d", tune.draw_limit,
                 tune.viewport_div, tune.nearest, tune.no_blend, tune.skip_2d, tune.skip_mesh);
    }
}

void HostSwap(int) {
    static unsigned long frame = 0;
    static double        last = 0.0;
    static double        swap_total = 0.0;
    static double        frame_total = 0.0;
    static uint64_t      draws = 0;
    static uint64_t      blits = 0;
    static uint64_t      uploads = 0;
    static uint64_t      upload_bytes = 0;
    static uint64_t      binds = 0;
    static uint64_t      orphans = 0;
    static double        phase[6] = {};
    static uint64_t      kinds[3] = {};
    static uint64_t      stream_bytes = 0;
    static double        finish_total = 0.0;
    // An empty file /data/darkcloud/ps4_gpu_time makes every frame wait for the GPU before the swap and
    // logs how long that wait is: the part of a frame the CPU's draw calls did not account for.
    static const bool gpu_time = access("/data/darkcloud/ps4_gpu_time", F_OK) == 0;
    if (gpu_time) {
        timespec a;
        clock_gettime(CLOCK_MONOTONIC, &a);
        glFinish();
        timespec b;
        clock_gettime(CLOCK_MONOTONIC, &b);
        finish_total += static_cast<double>(b.tv_sec - a.tv_sec) * 1000.0 + static_cast<double>(b.tv_nsec - a.tv_nsec) / 1.0e6;
    }
    timespec before;
    clock_gettime(CLOCK_MONOTONIC, &before);
    const double t0 = static_cast<double>(before.tv_sec) * 1000.0 + static_cast<double>(before.tv_nsec) / 1.0e6;
    ps4::SwapBuffers();
    timespec after;
    clock_gettime(CLOCK_MONOTONIC, &after);
    const double t1 = static_cast<double>(after.tv_sec) * 1000.0 + static_cast<double>(after.tv_nsec) / 1.0e6;
    frame++;
    if (frame % 30 == 0) {
        ReadTune();
    }
    swap_total += t1 - t0;
    if (last != 0.0) {
        frame_total += t1 - last;
    }
    last = t1;
    draws += g_stats.draws;
    blits += g_stats.blits;
    uploads += g_stats.uploads;
    upload_bytes += g_stats.upload_bytes;
    binds += g_stats.fbo_binds;
    orphans += g_stats.stream_orphans;
    phase[0] += g_stats.ms_prepare;
    phase[1] += g_stats.ms_data;
    phase[2] += g_stats.ms_issue;
    phase[3] += g_stats.ms_batch;
    phase[4] += g_stats.ms_target;
    phase[5] += g_stats.ms_texture;
    kinds[0] += g_stats.n_2d;
    kinds[1] += g_stats.n_immediate;
    kinds[2] += g_stats.n_retained;
    stream_bytes += g_stats.stream_bytes;
    g_stats = FrameStats{};
    {
        // The raw kernel clocks side by side (realtime, FreeBSD's id 1 CLOCK_VIRTUAL, id 4 monotonic): they should
        // advance together; a clock that lags is measuring CPU time.
        static double base[3] = {0, 0, 0};
        if (frame % 20 == 0) {
            double now[3];
            const int ids[3] = {0, 1, 4};
            for (int i = 0; i < 3; i++) {
                timespec ts{};
                sceKernelClockGettime(ids[i], &ts);
                now[i] = static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1.0e6;
            }
            if (base[0] != 0.0) {
                ps4::Log("[PS4] clocks over 20 frames: realtime %.0f ms, id1 %.0f ms, id4 %.0f ms", now[0] - base[0],
                         now[1] - base[1], now[2] - base[2]);
            }
            for (int i = 0; i < 3; i++) {
                base[i] = now[i];
            }
        }
    }
    if (frame <= 3 || frame % 20 == 0) {
        const double n = frame <= 3 ? 1.0 : 20.0;
        ps4::Log("[PS4] frame %lu: %.0f ms/frame (swap %.0f ms), per frame %.0f draws %.0f blits %.0f fbo binds, "
                 "%.1f uploads %.0f KB",
                 frame, frame_total / n, swap_total / n, static_cast<double>(draws) / n, static_cast<double>(blits) / n,
                 static_cast<double>(binds) / n, static_cast<double>(uploads) / n, static_cast<double>(upload_bytes) / n / 1024.0);
        ps4::Log("[PS4]   streamed %.0f KB a frame, %.2f buffer reallocations a frame, GPU wait before swap %.1f ms%s",
                 static_cast<double>(stream_bytes) / n / 1024.0, static_cast<double>(orphans) / n, finish_total / n,
                 gpu_time ? "" : " (not measured)");
        if (g_tune.timing) {
            ps4::Log("[PS4]   per frame: %.0f 2D + %.0f immediate mesh + %.0f retained mesh draws; CPU ms: prepare %.2f, "
                     "vertex data %.2f, draw calls %.2f, batch upload %.2f, targets/clears/copies %.2f, texture+mesh updates %.2f",
                     static_cast<double>(kinds[0]) / n, static_cast<double>(kinds[1]) / n,
                     static_cast<double>(kinds[2]) / n, phase[0] / n, phase[1] / n, phase[2] / n, phase[3] / n, phase[4] / n, phase[5] / n);
        }
        phase[0] = phase[1] = phase[2] = phase[3] = phase[4] = phase[5] = 0.0;
        kinds[0] = kinds[1] = kinds[2] = 0;
        swap_total = frame_total = finish_total = 0.0;
        draws = blits = uploads = upload_bytes = binds = orphans = stream_bytes = 0;
        if (frame == 20 || frame % 600 == 0) {
            ps4::LogMemoryStats("at frame");
        }
    }
    // A file /data/darkcloud/capture asks for a screenshot (it is removed when taken): /data/darkcloud/screenshot.png.
    if (frame % 15 == 0 && access("/data/darkcloud/capture", F_OK) == 0) {
        unlink("/data/darkcloud/capture");
        ps4::Log("[PS4] screenshot: %s", SaveLastPresented("/data/darkcloud/screenshot.png") ? "saved" : "nothing presented");
    }
    if (g_first_frame) {
        g_first_frame = false;
        ps4::HideSplashScreen();
    }
}

void HostWindowSize(int &width, int &height) { ps4::DisplaySize(width, height); }

} // namespace gfx::detail

#endif // __PS4__
