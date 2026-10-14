#pragma once

#include <cstdint>
#include <optional>
#include <vector>

struct SDL_Window;
union SDL_Event;

struct WindowConfig {
    // 0: the primary monitor's desktop resolution. With both 0 the window is fullscreen whatever
    // fullscreen says, so it has the monitor's pixels and shape. Headless, which has no monitor,
    // 1280x960.
    int  width = 0;
    int  height = 0;
    bool headless = false;
    bool fullscreen = false;
    // False for a renderer without a surface (gfx::RendererConfig::offscreen): SDL then loads no
    // Vulkan library and asks for no surface extension.
    bool vulkan = true;
};

// A window's mode: fullscreen, or a window whose inside is width by height (in the units of
// WindowSize). Two fullscreen modes are the same whatever their sizes.
struct WindowMode {
    bool fullscreen = false;
    int  width = 0;
    int  height = 0;

    bool operator==(const WindowMode &other) const {
        return fullscreen == other.fullscreen &&
               (fullscreen || (width == other.width && height == other.height));
    }
};

// What a WindowSetMode came to: the mode the window had, the one asked for and the one it has,
// and whether SDL took a request to enter (true) or leave (false) fullscreen, which may finish
// later. Headless or with no window, the modes are the default and the same.
struct WindowModeResult {
    WindowMode          before;
    WindowMode          requested;
    WindowMode          observed;
    std::optional<bool> fullscreen_request;

    bool Applied() const { return observed == requested; }
};

// What the window is called: its title, SDL's app identifier and its icon (icon_size x icon_size
// RGBA8). A null icon_rgba leaves the icon unset.
struct WindowIdentity {
    const char         *title = "";
    const char         *app_id = nullptr;
    const std::uint8_t *icon_rgba = nullptr;
    int                 icon_size = 0;
};

void WindowSetIdentity(const WindowIdentity &identity);

// Starts SDL's video subsystem and opens the window. On Windows a headless Vulkan window is hidden.
// Elsewhere headless uses SDL's offscreen driver, which gives Vulkan a VK_EXT_headless_surface, and
// SDL's dummy audio driver, which consumes the mix at the device rate without a device.
void WindowInit(const WindowConfig &config);
// Gives the window the size and fullscreen state WindowInit would have opened it with, on the
// display it is on; headless, nothing. The renderer follows through WindowPollEvents. A window
// already in that mode is left alone. SDL may refuse a step or not finish it before its wait ends;
// the result says what the window then is, and a size is not asked of a window still fullscreen.
WindowModeResult WindowSetMode(const WindowConfig &config);
// The window's mode now; false headless or with no window.
bool WindowCurrentMode(WindowMode &mode);
// Whether the window entered or left fullscreen or changed size since the last call, as an SDL
// change that finished late does.
bool        WindowTakeModeEvent();
void        WindowShutdown();
SDL_Window *WindowHandle();
// The window's size in the units mouse motion is measured in; false with no window.
bool WindowSize(int &width, int &height);
// The display's size, or with windowed the room a window has on it: the desktop's panels and the
// window's borders taken off. False headless or with no window.
bool WindowDisplaySize(int &width, int &height, bool windowed);

// A size in pixels, as a display lists its modes.
struct DisplayModeSize {
    int width = 0;
    int height = 0;
};

// The sizes the window's display offers fullscreen, each once whatever its refresh rates or pixel
// formats, the smallest first; empty headless or with no window.
std::vector<DisplayModeSize> WindowDisplayModes();
// Pumps events; false once the window is asked to close. A pixel-size change reaches the renderer.
bool WindowPollEvents();
// Sees every event WindowPollEvents pumps, before the window handles it.
void WindowAddEventHook(void (*hook)(const SDL_Event &event));
