#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>

#include "context.hpp"

// The SDL host of the ES 2 backend: an ES 2.0 context on the window the platform layer made with
// SDL_WINDOW_OPENGL. The PS4 build replaces this file with Piglet's EGL calls.

namespace gfx::detail {

namespace {

SDL_GLContext g_context = nullptr;
int           g_interval = -2;

} // namespace

bool HostCreateContext(SDL_Window *window, bool) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    g_context = SDL_GL_CreateContext(window);
    if (g_context == nullptr) {
        std::fprintf(stderr, "gfx: SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_GL_MakeCurrent(window, g_context)) {
        std::fprintf(stderr, "gfx: SDL_GL_MakeCurrent: %s\n", SDL_GetError());
        return false;
    }
    g_interval = -2;
    return true;
}

void HostDestroyContext() {
    if (g_context != nullptr) {
        SDL_GL_DestroyContext(g_context);
        g_context = nullptr;
    }
}

bool HostMakeCurrent(bool on) {
    return SDL_GL_MakeCurrent(g.window, on ? g_context : nullptr);
}

// GFX_RENDER_THREAD=1 runs the replay on its own thread (the PC test of what the console does).
bool HostRenderThreadWanted() { return std::getenv("GFX_RENDER_THREAD") != nullptr; }

void *HostGetProc(const char *name) { return reinterpret_cast<void *>(SDL_GL_GetProcAddress(name)); }

void HostSwap(int interval) {
    if (interval != g_interval) {
        SDL_GL_SetSwapInterval(interval);
        g_interval = interval;
    }
    SDL_GL_SwapWindow(g.window);
}

void HostWindowSize(int &width, int &height) { SDL_GetWindowSizeInPixels(g.window, &width, &height); }

} // namespace gfx::detail
