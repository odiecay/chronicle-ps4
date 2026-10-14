#pragma once

#include <functional>
#include <optional>

#include "settings.hpp"
#include "window.hpp"

// Keeps video.width, video.height and video.fullscreen true to the window. SDL may refuse a change
// of mode or finish it late, so the window is read back: after a DisplayApply that changes the
// window, or a pump that reads it back, the fields are the mode it is in (the fullscreen window's
// kept size and the --width and --height overrides aside). A window resized by hand changes no
// setting; runtime display edits go through DisplayApply. A warning stands until a later change of
// mode takes; while it does, or a fullscreen change SDL took is unfinished, the pump follows the
// window.

enum class DisplayWarning {
    None,
    // The window kept the mode it had.
    Kept,
    // The window is in a mode it was not asked for (DisplayShownMode).
    Changed,
};

// What the display drives and reads: the window's, unless a test replaces them.
struct DisplayWindow {
    WindowModeResult (*set_mode)(const WindowConfig &config) = WindowSetMode;
    bool (*current_mode)(WindowMode &mode) = WindowCurrentMode;
    bool (*take_event)() = WindowTakeModeEvent;
};

// Replaces the window, and forgets any warning.
void DisplayUseWindow(const DisplayWindow &window);

// The command line's --width and --height (0: none), which hold over the settings' size, and
// --headless, whose window keeps its own.
void DisplaySetOverrides(int width, int height, bool headless);

// The window the settings ask for, the overrides applied.
WindowConfig DisplayWindowConfig(const HostSettings &settings);

// Gives the window a changed size or mode between two of the settings. A reconciling change asks
// nothing of the window, whatever the overrides make of its fields.
void DisplayChanged(const HostSettings &before, const HostSettings &after);

// Runs change (the game's own settings change), then the display fields made
// the window's mode where the window did not take the one asked for. Returns whether the settings
// are saved.
bool DisplayApply(const std::function<bool()> &change);

// For the host after it pumps the window's events, outside any settings change: while a warning
// stands or a fullscreen change is unfinished, a change of the window's mode, as SDL finishing late
// makes, becomes the display fields, from the latest ones asked for, and the warning goes if it is
// the mode they ask.
void DisplayPump();

// Whether the settings were saved at the last DisplayPump that changed them, once.
std::optional<bool> DisplayTakePumpSave();

// What the display came to at the last change of its mode.
DisplayWarning DisplayGetWarning();

// The window's mode when the warning was given.
WindowMode DisplayShownMode();
