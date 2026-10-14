#pragma once

#include <string>
#include <string_view>
#include <vector>

enum class ConfigPresentMode {
    Fifo,
    Mailbox,
    Immediate,
};

// What the FPS counter shows: the frame rate alone, with the logic ticks, or all of it with the draws.
enum class ConfigFpsDetail {
    Fps,
    Ticks,
    All,
};

enum class ConfigGyro {
    Off,
    Always,
    FirstPerson,
    Held,
};

// input.glyph_device: whose button symbols the game draws. Auto follows the device in use (the
// last of keyboard/mouse or gamepad to be touched; a gamepad's own family; Xbox for a pad of unknown make), PS4 until one is.
enum class ConfigGlyphDevice {
    Auto,
    Ps3,
    Ps4,
    Ps5,
    Ps5Color,
    Xbox,
    Switch,
    SteamDeck,
    SteamController,
    Keyboard,
};

struct ConfigKeyBinding {
    std::string              action;
    std::vector<std::string> keys;

    bool operator==(const ConfigKeyBinding &) const = default;
};

struct HostSettings {
    double            tick_rate = 60.0;
    ConfigPresentMode present_mode = ConfigPresentMode::Fifo;
    double            max_fps = 0.0;
    // 0: the monitor's (WindowConfig).
    int                           window_width = 0;
    int                           window_height = 0;
    bool                          fullscreen = false;
    bool                          show_fps = false;
    ConfigFpsDetail               fps_detail = ConfigFpsDetail::All;
    float                         master_volume = 1.0f;
    bool                          surround = false;
    // audio.soundtrack "custom": play recordings from soundtrack/ beside save/ and data/.
    bool                          soundtrack = false;
    std::vector<ConfigKeyBinding> key_bindings;
    float                         mouse_sensitivity = 0.1f;
    float                         stick_sensitivity = 1.33f;
    bool                          stick_invert_x = false;
    bool                          stick_invert_y = false;
    ConfigGyro                    gyro = ConfigGyro::Held;
    float                         gyro_sensitivity = 0.5f;
    bool                          gyro_invert_x = false;
    bool                          gyro_invert_y = false;
    bool                          mouse_invert_y = false;
    bool                          mouse_capture = true;
    bool                          mouse_zoom = false;
    // A DualSense's touchpad as the menus' mouse, at this speed (1 is a swipe across the pad for 1500
    // window pixels), and its lightbar showing the active character's life.
    bool                          touchpad = true;
    float                         touchpad_sensitivity = 1.0f;
    bool                          lightbar = true;
    // Scales a gamepad's rumble motors, 0 to 1.
    float                         rumble_strength = 1.0f;
    // input.glyphs "new": the button symbols of glyphs/ (see glyphs.hpp); "original": the game's own.
    bool                          glyphs_new = true;
    ConfigGlyphDevice             glyph_device = ConfigGlyphDevice::Auto;
    std::vector<std::string>      mouse_release_keys;

    bool operator==(const HostSettings &) const = default;
};

// The game's environment variables are <prefix>_<name>.
void        SettingsSetEnvPrefix(std::string_view prefix);
const char *SettingsEnv(std::string_view name);   // std::getenv("<prefix>_<name>"), nullptr if unset or no prefix was set

// Provided by the game: its settings, and a change that applies the host fields while keeping the
// game's own.
const HostSettings &SettingsGet();
bool                SettingsChange(const HostSettings &settings);
