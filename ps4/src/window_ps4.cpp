// platform/window.hpp for the PS4: there is one fixed full-screen "window" and no events.

#ifdef __PS4__

#include "platform/window.hpp"

#include <unistd.h>

#include "ps4_platform.hpp"

namespace {

// 1080p by default; an empty /data/darkcloud/ps4_720p file selects 720p, which costs the GPU and the
// console's video memory about 40% less.
int g_width = ps4::kDisplayWidth;
int g_height = ps4::kDisplayHeight;
int g_dummy_window = 0;
bool g_open = false;

} // namespace

namespace ps4 {

void DisplaySize(int &width, int &height) {
    width = g_width;
    height = g_height;
}

} // namespace ps4

void WindowSetIdentity(const WindowIdentity &) {}

void WindowInit(const WindowConfig &) {
    if (access("/data/darkcloud/ps4_720p", F_OK) == 0) {
        g_width = 1280;
        g_height = 720;
    }
    g_open = true;
    ps4::Log("[PS4] display %dx%d", g_width, g_height);
}

WindowModeResult WindowSetMode(const WindowConfig &) {
    WindowModeResult result;
    result.requested = result.observed = result.before = WindowMode{true, g_width, g_height};
    return result;
}

bool WindowCurrentMode(WindowMode &mode) {
    mode = WindowMode{true, g_width, g_height};
    return g_open;
}

bool WindowTakeModeEvent() { return false; }

void WindowShutdown() { g_open = false; }

SDL_Window *WindowHandle() { return reinterpret_cast<SDL_Window *>(&g_dummy_window); }

bool WindowSize(int &width, int &height) {
    width = g_width;
    height = g_height;
    return g_open;
}

bool WindowDisplaySize(int &width, int &height, bool) {
    width = g_width;
    height = g_height;
    return g_open;
}

std::vector<DisplayModeSize> WindowDisplayModes() { return {{1280, 720}, {1920, 1080}}; }

bool WindowPollEvents() { return true; }

void WindowAddEventHook(void (*)(const SDL_Event &)) {}

#endif // __PS4__
