#include "platform/stdcompat.hpp"
#include "input.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "clock.hpp"
#include "settings.hpp"
#include "gamepad_map.hpp"
#include "mouse.hpp"
#include "touchpad.hpp"
#include "window.hpp"

namespace {

enum class ActionKind {
    Button,
    HalfAxis,
    Axis,
    Host,
};

enum Axis {
    kAxisLeftX,
    kAxisLeftY,
    kAxisRightX,
    kAxisRightY,
};

struct Action {
    std::string_view name;
    ActionKind       kind;
    std::uint16_t    button;
    Axis             axis;
    int              direction;
    std::string_view defaults;
};

// The game-side reasons for each default are in docs/PC.md ("Keyboard and mouse").
// clang-format off
constexpr Action kActions[] = {
    {"up",           ActionKind::Button,   kInputUp,       kAxisLeftX,  0,  "Up"},
    {"down",         ActionKind::Button,   kInputDown,     kAxisLeftX,  0,  "Down"},
    {"left",         ActionKind::Button,   kInputLeft,     kAxisLeftX,  0,  "Left"},
    {"right",        ActionKind::Button,   kInputRight,    kAxisLeftX,  0,  "Right"},
    {"cross",        ActionKind::Button,   kInputCross,    kAxisLeftX,  0,  "Mouse1, Space"},
    {"circle",       ActionKind::Button,   kInputCircle,   kAxisLeftX,  0,  "Mouse2"},
    {"square",       ActionKind::Button,   kInputSquare,   kAxisLeftX,  0,  "E"},
    {"triangle",     ActionKind::Button,   kInputTriangle, kAxisLeftX,  0,  "Tab"},
    {"l1",           ActionKind::Button,   kInputL1,       kAxisLeftX,  0,  "Z"},
    {"r1",           ActionKind::Button,   kInputR1,       kAxisLeftX,  0,  "F, X"},
    {"l2",           ActionKind::Button,   kInputL2,       kAxisLeftX,  0,  "Q"},
    {"r2",           ActionKind::Button,   kInputR2,       kAxisLeftX,  0,  "R"},
    {"l3",           ActionKind::Button,   kInputL3,       kAxisLeftX,  0,  "V"},
    {"r3",           ActionKind::Button,   kInputR3,       kAxisLeftX,  0,  "Mouse3, B"},
    {"start",        ActionKind::Button,   kInputStart,    kAxisLeftX,  0,  "Return"},
    {"select",       ActionKind::Button,   kInputSelect,   kAxisLeftX,  0,  "Backspace, C"},
    {"lx-",          ActionKind::HalfAxis, 0,              kAxisLeftX,  -1, "A"},
    {"lx+",          ActionKind::HalfAxis, 0,              kAxisLeftX,  1,  "D"},
    {"ly-",          ActionKind::HalfAxis, 0,              kAxisLeftY,  -1, "W"},
    {"ly+",          ActionKind::HalfAxis, 0,              kAxisLeftY,  1,  "S"},
    {"rx-",          ActionKind::HalfAxis, 0,              kAxisRightX, -1, "J"},
    {"rx+",          ActionKind::HalfAxis, 0,              kAxisRightX, 1,  "L"},
    {"ry-",          ActionKind::HalfAxis, 0,              kAxisRightY, -1, "I"},
    {"ry+",          ActionKind::HalfAxis, 0,              kAxisRightY, 1,  "K"},
    {"lx",           ActionKind::Axis,     0,              kAxisLeftX,  0,  ""},
    {"ly",           ActionKind::Axis,     0,              kAxisLeftY,  0,  ""},
    {"rx",           ActionKind::Axis,     0,              kAxisRightX, 0,  ""},
    {"ry",           ActionKind::Axis,     0,              kAxisRightY, 0,  ""},
    {"fps_toggle",   ActionKind::Host,     0,              kAxisLeftX,  0,  "F3"},
    {"developer_menu", ActionKind::Host,   0,              kAxisLeftX,  0,  "Gamepad:paddle2"},
    {"debug_menu",   ActionKind::Host,     0,              kAxisLeftX,  0,  "Gamepad:paddle1"},
    {"gyro_hold",    ActionKind::Host,     0,              kAxisLeftX,  0,  "Gamepad:paddle4"},
    {"zoom_reset",   ActionKind::Host,     0,              kAxisLeftX,  0,  "Mouse3"},
};

// Every gamepad button the game reads; GamepadButtonPad (gamepad_map.hpp) says which DualShock 2
// button each one presses, which depends on the pad's type for Back and the touchpad click.
// clang-format off
constexpr SDL_GamepadButton kGamepadButtons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,
    SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_TOUCHPAD,
    SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
};
// clang-format on

constexpr std::size_t kActionCount = std::size(kActions);
constexpr std::size_t kFirstHostAction = kActionCount - 5;
constexpr std::size_t kHostActionCount = kActionCount - kFirstHostAction;
static_assert(kActions[kFirstHostAction].name == "fps_toggle");
static_assert(static_cast<std::size_t>(InputHostAction::FpsToggle) == 0);
static_assert(static_cast<std::size_t>(InputHostAction::ZoomReset) == kHostActionCount - 1);
constexpr std::size_t kZoomResetHost = static_cast<std::size_t>(InputHostAction::ZoomReset);

// AxisCalibration (ps2/src/gamepad.cpp): a byte within 49 above or 50 below the centre reads as
// zero, and the remaining 78 steps each side span the game's +-128.
constexpr int kDeadZoneAbove = 49;
constexpr int kDeadZoneBelow = 50;
constexpr int kLiveSpan = 78;
constexpr int kGameAxisRange = 128;

// The DualShock 2's L2 and R2 are digital in the mode the game uses.
constexpr Sint16 kTriggerThreshold = SDL_JOYSTICK_AXIS_MAX / 4;

// SDL rumble needs a duration; the game restates its motors every frame
// through scePadSetActDirect, so a short one is refreshed and a hung game
// stops shaking the pad on its own.
constexpr Uint32 kRumbleMilliseconds = 500;

constexpr int kMouseButtonCount = 5;

constexpr float kGyroDeadband = 0.03f;
// A stick bound to the mouse takes the deflection that turns the dungeon's camera (0.04 radians a
// tick at full deflection, dun/gameloop.cpp:4336) as far as the mouse look would.
constexpr float kMouseStickRadians = 0.04f;

// Longer between two reads of pad 0 and the game was loading, not looking.
constexpr double kMouseLookMaxGapSeconds = 0.25;

struct Source {
    enum Kind {
        Key,
        MouseButton,
        MouseAxis,
        GamepadButton,
    };

    Kind  kind = Key;
    int   code = 0;
    float scale = 1.0f;
};

struct PadSlot {
    SDL_Gamepad *gamepad = nullptr;
    InputRumble  rumble;
    bool         rumble_sent = false;
    Uint64       rumble_time = 0;
    // The lightbar colour the game asked for, kept to restore when the pad reconnects.
    bool         led_wanted = false;
    Uint8        led[3] = {};
};

// Which device the player touched last (InputActiveGlyphFamily): false until a key or the mouse is
// used, and the make of the first gamepad once one is open.
bool             g_last_keyboard = false;
InputGlyphFamily g_pad_family = InputGlyphFamily::Ps4;

std::array<std::vector<Source>, kActionCount>            g_bindings;
bool                                                     g_bindings_ready = false;
InputMouseSettings                                       g_mouse;
std::array<PadSlot, kInputPadCount>                      g_slots;
std::array<InputPadState, kInputPadCount>                g_device;
std::array<InputPadState, kInputPadCount>                g_state;
std::array<InputPadState, kInputPadCount>                g_view;
std::array<std::optional<InputPadState>, kInputPadCount> g_override;
std::array<bool, SDL_SCANCODE_COUNT>                     g_keys{};
float                                                    g_mouse_dx = 0.0f;
float                                                    g_mouse_dy = 0.0f;
InputMouseLook                                           g_mouse_look;
bool                                                     g_mouse_zoom = false;
float                                                    g_scripted_wheel = 0.0f;
std::int64_t                                             g_last_latch_tick = -1;
std::int64_t                                             g_latch_serial = 0;
std::int64_t                                             g_stick_read_serial = -1;
bool                                                     g_stick_live = false;
bool                                                     g_gamepad_subsystem = false;
InputKeyboardMouse                                       g_scripted;
// The mouse is a menu's pointer (InputSetMenuMouse); the motion it has gathered for it.
bool  g_menu_mouse = false;
bool  g_menu_navigation = false;
bool  g_developer_menu = false;
float g_menu_dx = 0.0f;
float g_menu_dy = 0.0f;
// A DualSense's touchpad as a menu pointer (platform/touchpad.hpp): its motion and scroll since the
// last take, the buttons it holds, and the taps still to show as a click, for two takes.
TouchpadGestures g_touchpad;
bool             g_touchpad_on = true;
bool             g_lightbar_on = true;
float            g_rumble_strength = 1.0f;
float            g_touch_dx = 0.0f;
float            g_touch_dy = 0.0f;
float            g_touch_wheel = 0.0f;
std::uint32_t    g_touch_held = 0;
std::uint32_t    g_touch_tap = 0;
bool             g_touch_tap_shown = false;
// Per host action: presses not yet consumed, and whether its non-key sources (mouse and gamepad
// buttons, the script's keys) were held at the last poll, for their press edges.
std::array<int, kHostActionCount>  g_host_presses{};
std::array<bool, kHostActionCount> g_host_polled{};

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool ParseSource(std::string_view name, ActionKind kind, Source &source) {
    std::string lower = Lower(Trim(name));
    if (kind == ActionKind::Axis) {
        std::string_view text = lower;
        float            sign = 1.0f;
        if (text.starts_with('-')) {
            sign = -1.0f;
            text = Trim(text.substr(1));
        }
        float       scale = 1.0f;
        std::size_t star = text.find('*');
        if (star != std::string_view::npos) {
            std::string_view factor = Trim(text.substr(star + 1));
            auto [end, error] = FromChars(factor.data(), factor.data() + factor.size(), scale);
            if (error != std::errc{} || end != factor.data() + factor.size() || !std::isfinite(scale)) {
                return false;
            }
            text = Trim(text.substr(0, star));
        }
        if (text != "mousex" && text != "mousey") {
            return false;
        }
        source = {Source::MouseAxis, text == "mousex" ? 0 : 1, sign * scale};
        return true;
    }
    if (kind == ActionKind::Host && lower.starts_with("gamepad:")) {
        std::string       button_name = lower.substr(8);
        SDL_GamepadButton button = SDL_GetGamepadButtonFromString(button_name.c_str());
        if (button == SDL_GAMEPAD_BUTTON_INVALID) {
            return false;
        }
        source = {Source::GamepadButton, static_cast<int>(button), 1.0f};
        return true;
    }
    bool button = lower.size() == 6 && lower.starts_with("mouse");
    if (button && lower[5] >= '1' && lower[5] < '1' + kMouseButtonCount) {
        source = {Source::MouseButton, lower[5] - '0', 1.0f};
        return true;
    }
    int scancode = InputScancodeFromName(Trim(name));
    if (scancode < 0) {
        return false;
    }
    source = {Source::Key, scancode, 1.0f};
    return true;
}

bool ParseSources(std::span<const std::string_view> names, ActionKind kind, std::vector<Source> &sources) {
    sources.clear();
    for (std::string_view name : names) {
        Source source;
        if (!ParseSource(name, kind, source)) {
            return false;
        }
        if (source.kind == Source::Key && source.code == SDL_SCANCODE_ESCAPE) {
            return false;
        }
        sources.push_back(source);
    }
    return true;
}

std::vector<std::string_view> SplitDefaults(std::string_view text) {
    std::vector<std::string_view> names;
    while (!text.empty()) {
        std::size_t comma = text.find(',');
        names.push_back(Trim(text.substr(0, comma)));
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    }
    return names;
}

void EnsureBindings() {
    if (g_bindings_ready) {
        return;
    }
    for (std::size_t i = 0; i < kActionCount; ++i) {
        std::vector<std::string_view> names = SplitDefaults(kActions[i].defaults);
        ParseSources(names, kActions[i].kind, g_bindings[i]);
    }
    g_mouse = InputMouseSettings{};
    g_mouse_zoom = false;
    MouseConfigure(g_mouse.capture, g_mouse.release_scancodes);
    g_bindings_ready = true;
}

std::uint8_t StickToByte(Sint16 value) {
    return static_cast<std::uint8_t>((static_cast<int>(value) + 32768) >> 8);
}

// The DualShock 2 read full deflection well before its stick's stop and reached its corners on a
// diagonal; a modern stick reports a circle and reaches full only at the stop. The game's thresholds
// expect the former (the town runs past a magnitude of 0.85, after AxisCalibration's dead zone,
// which needs over 90% of a modern stick's travel), so the deflection is scaled by
// input.stick_sensitivity (1.33 by default, PCSX2's analog sensitivity) and the circle is stretched
// onto the square, keeping the direction.
float      g_stick_sensitivity = 1.33f;
float      g_gyro_sensitivity = 0.5f;
ConfigGyro g_gyro = ConfigGyro::Off;
bool       g_gyro_invert_x = false;
bool       g_gyro_invert_y = false;
bool       g_stick_invert_x = false;
bool       g_stick_invert_y = false;
bool       g_look_left = false;

void StickPairToBytes(Sint16 x, Sint16 y, std::uint8_t &byte_x, std::uint8_t &byte_y) {
    float fx = std::clamp(static_cast<float>(x) / 32767.0f, -1.0f, 1.0f);
    float fy = std::clamp(static_cast<float>(y) / 32767.0f, -1.0f, 1.0f);
    float radius = std::min(1.0f, std::hypot(fx, fy) * g_stick_sensitivity);
    float edge = std::max(std::fabs(fx), std::fabs(fy));
    if (edge > 0.0f) {
        fx *= radius / edge;
        fy *= radius / edge;
    }
    byte_x = StickToByte(static_cast<Sint16>(std::lround(std::clamp(fx, -1.0f, 1.0f) * 32767.0f)));
    byte_y = StickToByte(static_cast<Sint16>(std::lround(std::clamp(fy, -1.0f, 1.0f) * 32767.0f)));
}

bool Deflected(std::uint8_t byte) {
    int offset = static_cast<int>(byte) - kInputAxisCentre;
    return offset > kDeadZoneAbove || offset < -kDeadZoneBelow;
}

std::uint8_t &AxisOf(InputPadState &state, Axis axis) {
    switch (axis) {
        case kAxisLeftX:
            return state.left_x;
        case kAxisLeftY:
            return state.left_y;
        case kAxisRightX:
            return state.right_x;
        case kAxisRightY:
            break;
    }
    return state.right_y;
}

Sint16 Flip(Sint16 value) {
    return value == SDL_JOYSTICK_AXIS_MIN ? SDL_JOYSTICK_AXIS_MAX : static_cast<Sint16>(-value);
}

bool GyroActive() {
    switch (g_gyro) {
        case ConfigGyro::Always:
            return true;
        case ConfigGyro::FirstPerson:
            return g_look_left;
        case ConfigGyro::Held:
            return InputHostHeld(InputHostAction::GyroHold);
        default:
            return false;
    }
}

void ReadGamepad(SDL_Gamepad *gamepad, InputPadState &state) {
    SDL_GamepadType type = SDL_GetGamepadType(gamepad);
    for (SDL_GamepadButton button : kGamepadButtons) {
        if (SDL_GetGamepadButton(gamepad, button)) {
            state.buttons |= GamepadButtonPad(type, button);
        }
    }
    if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerThreshold) {
        state.buttons |= kInputL2;
    }
    if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerThreshold) {
        state.buttons |= kInputR2;
    }
    Sint16 axes[4] = {SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX),
                      SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY),
                      SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX),
                      SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY)};
    auto   invert = [&](int x) {
        if (g_stick_invert_x) {
            axes[x] = Flip(axes[x]);
        }
        if (g_stick_invert_y) {
            axes[x + 1] = Flip(axes[x + 1]);
        }
    };
    invert(2);
    if (g_look_left) {
        invert(0);
    }
    StickPairToBytes(axes[0], axes[1], state.left_x, state.left_y);
    StickPairToBytes(axes[2], axes[3], state.right_x, state.right_y);
    float rate[3];
    if (!GyroActive() || Deflected(state.right_x) || Deflected(state.right_y) ||
        !SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_GYRO, rate, 3)) {
        return;
    }
    float yaw = std::fabs(rate[1]) > kGyroDeadband ? -rate[1] * g_gyro_sensitivity : 0.0f;
    float pitch = std::fabs(rate[0]) > kGyroDeadband ? -rate[0] * g_gyro_sensitivity : 0.0f;
    state.right_x = InputStickByte(g_gyro_invert_x ? -yaw : yaw);
    state.right_y = InputStickByte(g_gyro_invert_y ? -pitch : pitch);
}

// Sets a pad's lightbar if it has one.
void ApplyLightbar(const PadSlot &slot) {
    if (slot.gamepad == nullptr || !slot.led_wanted || !g_lightbar_on) {
        return;
    }
    SDL_PropertiesID properties = SDL_GetGamepadProperties(slot.gamepad);
    if (SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false)) {
        SDL_SetGamepadLED(slot.gamepad, slot.led[0], slot.led[1], slot.led[2]);
    }
}

// Folds pad 0's touchpad, if it has one, into the menu pointer's inputs.
void ReadTouchpad(SDL_Gamepad *gamepad) {
    if (gamepad == nullptr || !g_touchpad_on || SDL_GetNumGamepadTouchpads(gamepad) < 1) {
        g_touchpad.Reset();
        g_touch_held = 0;
        return;
    }
    TouchpadFrame frame;
    int           fingers = std::min(2, SDL_GetNumGamepadTouchpadFingers(gamepad, 0));
    for (int i = 0; i < fingers; ++i) {
        bool  down = false;
        float x = 0.0f;
        float y = 0.0f;
        if (SDL_GetGamepadTouchpadFinger(gamepad, 0, i, &down, &x, &y, nullptr)) {
            frame.finger[i] = {down, x, y};
        }
    }
    frame.click = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
    frame.create = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC1);
    TouchpadStep step = g_touchpad.Update(frame, SDL_GetTicks());
    g_touch_held = step.held;
    if (!g_menu_mouse) {
        // Not a pointer's screen: the pad's gestures mean nothing and must not wait for the next.
        g_touch_dx = g_touch_dy = g_touch_wheel = 0.0f;
        g_touch_tap = 0;
        return;
    }
    g_touch_dx += step.dx;
    g_touch_dy += step.dy;
    g_touch_wheel += step.wheel;
    if (step.tapped != 0) {
        g_touch_tap |= step.tapped;
        g_touch_tap_shown = false;
    }
}

void EnableGyro(SDL_Gamepad *gamepad) {
    if (SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO)) {
        SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, g_gyro != ConfigGyro::Off);
    }
}

InputKeyboardMouse LiveKeyboardMouse() {
    InputKeyboardMouse held;
    for (int scancode = 0; scancode < SDL_SCANCODE_COUNT; ++scancode) {
        if (g_keys[scancode]) {
            held.keys.push_back(scancode);
        }
    }
    held.mouse_buttons = MouseButtons();
    held.mouse_dx = g_mouse_dx;
    held.mouse_dy = g_mouse_dy;
    return held;
}

void Compose(int pad) {
    g_state[pad] = pad == 0 ? InputApplyKeyboardMouse(g_device[pad], LiveKeyboardMouse()) : g_device[pad];
    InputPadState &state = g_state[pad];
    if (pad == 0 && g_look_left && !Deflected(state.left_x) && !Deflected(state.left_y)) {
        state.left_x = state.right_x;
        state.left_y = state.right_y;
    }
}

void SyncGamepads() {
    SDL_UpdateGamepads();
    for (PadSlot &slot : g_slots) {
        if (slot.gamepad != nullptr && !SDL_GamepadConnected(slot.gamepad)) {
            SDL_CloseGamepad(slot.gamepad);
            slot = PadSlot{};
        }
    }
    int             count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids == nullptr) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        bool open = false;
        for (const PadSlot &slot : g_slots) {
            open |= slot.gamepad != nullptr && SDL_GetGamepadID(slot.gamepad) == ids[i];
        }
        if (open) {
            continue;
        }
        for (PadSlot &slot : g_slots) {
            if (slot.gamepad == nullptr) {
                slot.gamepad = SDL_OpenGamepad(ids[i]);
                if (slot.gamepad == nullptr) {
                    std::fprintf(stderr, "input: SDL_OpenGamepad: %s\n", SDL_GetError());
                } else {
                    EnableGyro(slot.gamepad);
                    SDL_SetGamepadPlayerIndex(slot.gamepad, static_cast<int>(&slot - g_slots.data()));
                    ApplyLightbar(slot);
                }
                break;
            }
        }
    }
    SDL_free(ids);
}

void SendRumble(int pad) {
    PadSlot &slot = g_slots[pad];
    if (slot.gamepad == nullptr) {
        return;
    }
    Uint16 low = static_cast<Uint16>(slot.rumble.large_motor * 257 * g_rumble_strength);
    Uint16 high = slot.rumble.small_motor ? static_cast<Uint16>(0xFFFF * g_rumble_strength) : 0;
    SDL_RumbleGamepad(slot.gamepad, low, high, kRumbleMilliseconds);
    slot.rumble_sent = true;
    slot.rumble_time = SDL_GetTicks();
}

bool GamepadButtonHeld(int button) {
    return std::ranges::any_of(g_slots, [&](const PadSlot &slot) {
        return slot.gamepad != nullptr &&
               SDL_GetGamepadButton(slot.gamepad, static_cast<SDL_GamepadButton>(button));
    });
}

bool KeyBound(std::size_t host, int scancode) {
    return std::ranges::any_of(g_bindings[kFirstHostAction + host], [&](const Source &source) {
        return source.kind == Source::Key && source.code == scancode;
    });
}

// Everything but the live keys, which count their presses as their events arrive.
bool HostPolledHeld(std::size_t host) {
    for (const Source &source : g_bindings[kFirstHostAction + host]) {
        bool held = false;
        switch (source.kind) {
            case Source::Key:
                held = std::ranges::find(g_scripted.keys, source.code) != g_scripted.keys.end();
                break;
            case Source::MouseButton:
                held = ((MouseButtons() | g_scripted.mouse_buttons) & (1u << (source.code - 1))) != 0;
                break;
            case Source::GamepadButton:
                held = GamepadButtonHeld(source.code);
                break;
            case Source::MouseAxis:
                break;
        }
        if (held) {
            return true;
        }
    }
    return false;
}

bool MouseAxisBound(int code) {
    return std::ranges::any_of(g_bindings, [&](const std::vector<Source> &sources) {
        return std::ranges::any_of(sources, [&](const Source &source) {
            return source.kind == Source::MouseAxis && source.code == code;
        });
    });
}

// A zoom reset binding owns its physical sources while zoom is enabled. Keyboard alternatives
// of the same game action and actual controller buttons still work normally.
bool ZoomReserved(const Source &source) {
    return g_mouse_zoom && !g_menu_mouse &&
           std::ranges::any_of(g_bindings[kFirstHostAction + kZoomResetHost], [&](const Source &reset) {
               return reset.kind == source.kind && reset.code == source.code;
           });
}

float MouseRadiansPerCount() { return g_mouse.sensitivity * std::numbers::pi_v<float> / 180.0f; }

InputMouseLook MouseLookFromMotion(float dx, float dy) {
    InputMouseLook look;
    if (!MouseAxisBound(0)) {
        look.yaw = dx * MouseRadiansPerCount();
    }
    if (!MouseAxisBound(1)) {
        look.pitch = (g_mouse.invert_y ? dy : -dy) * MouseRadiansPerCount();
    }
    return look;
}

void PollHostActions() {
    for (std::size_t host = 0; host < kHostActionCount; ++host) {
        bool held = HostPolledHeld(host);
        if (host == kZoomResetHost && (!g_mouse_zoom || g_menu_mouse)) {
            g_host_presses[host] = 0;
            g_host_polled[host] = held;
            continue;
        }
        if (held && !g_host_polled[host]) {
            ++g_host_presses[host];
        }
        g_host_polled[host] = held;
    }
}

// A rebind capture in progress: the keys, mouse buttons and gamepad buttons that were already held
// at the last call, so the next call reports only what has gone down since.
bool                                 g_bind_capture = false;
std::array<bool, SDL_SCANCODE_COUNT> g_bind_keys{};
std::uint32_t                        g_bind_mouse = 0;
std::uint16_t                        g_bind_pad = 0;

bool BindKeyHeld(int scancode) {
    return g_keys[scancode] || std::ranges::find(g_scripted.keys, scancode) != g_scripted.keys.end();
}

// The gamepad buttons held now, one bit per entry of kGamepadButtons.
std::uint16_t BindGamepadHeld() {
    std::uint16_t held = 0;
    for (std::size_t i = 0; i < std::size(kGamepadButtons); ++i) {
        bool down = std::ranges::any_of(g_slots, [&](const PadSlot &slot) {
            return slot.gamepad != nullptr && SDL_GetGamepadButton(slot.gamepad, kGamepadButtons[i]);
        });
        if (down) {
            held |= static_cast<std::uint16_t>(1u << i);
        }
    }
    return held;
}

void SnapshotBind() {
    for (int scancode = 0; scancode < SDL_SCANCODE_COUNT; ++scancode) {
        g_bind_keys[scancode] = BindKeyHeld(scancode);
    }
    g_bind_mouse = MouseButtons() | g_scripted.mouse_buttons;
    g_bind_pad = BindGamepadHeld();
}

} // namespace

void InputInit() {
    EnsureBindings();
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        g_gamepad_subsystem = true;
    } else {
        std::fprintf(stderr, "input: no gamepads: %s\n", SDL_GetError());
    }
    InputApplyConfig(SettingsGet());
    WindowAddEventHook(InputHandleEvent);
    MouseStart();
}

void InputApplyConfig(const HostSettings &config) {
    InputResetBindings();
    for (const ConfigKeyBinding &binding : config.key_bindings) {
        std::vector<std::string_view> names(binding.keys.begin(), binding.keys.end());
        if (!InputBindKeys(binding.action, names)) {
            std::fprintf(stderr, "input: cannot bind %s\n", binding.action.c_str());
        }
    }
    g_stick_sensitivity = config.stick_sensitivity;
    g_gyro_sensitivity = config.gyro_sensitivity;
    g_touchpad_on = config.touchpad;
    g_touchpad.SetSensitivity(config.touchpad_sensitivity);
    g_lightbar_on = config.lightbar;
    g_rumble_strength = config.rumble_strength;
    for (const PadSlot &slot : g_slots) {
        ApplyLightbar(slot);
    }
    g_gyro_invert_x = config.gyro_invert_x;
    g_gyro_invert_y = config.gyro_invert_y;
    g_stick_invert_x = config.stick_invert_x;
    g_stick_invert_y = config.stick_invert_y;
    if (config.gyro != g_gyro) {
        g_gyro = config.gyro;
        for (PadSlot &slot : g_slots) {
            if (slot.gamepad != nullptr) {
                EnableGyro(slot.gamepad);
            }
        }
    }
    InputMouseSettings mouse;
    mouse.sensitivity = config.mouse_sensitivity;
    mouse.invert_y = config.mouse_invert_y;
    mouse.capture = config.mouse_capture;
    for (const std::string &name : config.mouse_release_keys) {
        int scancode = InputScancodeFromName(name);
        if (scancode < 0) {
            std::fprintf(stderr, "input: unknown mouse_release key %s\n", name.c_str());
            continue;
        }
        mouse.release_scancodes.push_back(scancode);
    }
    InputSetMouseSettings(mouse);
    g_mouse_zoom = config.mouse_zoom;
    g_mouse_look.zoom = 0.0f;
    g_mouse_look.zoom_reset = false;
    g_scripted_wheel = 0.0f;
    MouseTakeWheel();
    g_host_presses[kZoomResetHost] = 0;
    g_host_polled[kZoomResetHost] = HostPolledHeld(kZoomResetHost);
}

void InputShutdown() {
    InputSetMovementLocked(false);
    MouseStop();
    g_menu_mouse = false;
    g_menu_navigation = false;
    g_developer_menu = false;
    g_menu_dx = 0.0f;
    g_menu_dy = 0.0f;
    g_touchpad.Reset();
    g_touch_dx = g_touch_dy = g_touch_wheel = 0.0f;
    g_touch_held = g_touch_tap = 0;
    g_mouse_look = {0.0f, 0.0f, g_mouse_look.read};
    g_keys.fill(false);
    g_scripted = {};
    g_scripted_wheel = 0.0f;
    g_host_presses.fill(0);
    g_host_polled.fill(false);
    g_gyro = ConfigGyro::Off;
    for (PadSlot &slot : g_slots) {
        if (slot.gamepad != nullptr) {
            SDL_CloseGamepad(slot.gamepad);
        }
        slot = PadSlot{};
    }
    if (g_gamepad_subsystem) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        g_gamepad_subsystem = false;
    }
}

InputGlyphFamily InputGlyphFamilyForGamepad(int sdl_gamepad_type, unsigned vendor, unsigned product) {
    constexpr unsigned kValve = 0x28DE;
    constexpr unsigned kSteamDeck = 0x1205;
    switch (sdl_gamepad_type) {
        case SDL_GAMEPAD_TYPE_PS3:
            return InputGlyphFamily::Ps3;
        case SDL_GAMEPAD_TYPE_PS4:
            return InputGlyphFamily::Ps4;
        case SDL_GAMEPAD_TYPE_PS5:
            return InputGlyphFamily::Ps5;
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE:
            return InputGlyphFamily::Xbox;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return InputGlyphFamily::Switch;
        default:
            break;
    }
    if (vendor == kValve) {
        return product == kSteamDeck ? InputGlyphFamily::SteamDeck : InputGlyphFamily::SteamController;
    }
    return InputGlyphFamily::Xbox;
}

static InputGlyphFamily GamepadFamily(SDL_Gamepad *gamepad) {
    return InputGlyphFamilyForGamepad(SDL_GetGamepadType(gamepad), SDL_GetGamepadVendor(gamepad),
                                      SDL_GetGamepadProduct(gamepad));
}

void InputPoll() {
    EnsureBindings();
    if (g_gamepad_subsystem) {
        SyncGamepads();
    }
    if (g_slots[0].gamepad != nullptr) {
        g_pad_family = GamepadFamily(g_slots[0].gamepad);
    }
    for (int pad = 0; pad < kInputPadCount; ++pad) {
        InputPadState state;
        state.connected = pad == 0 || g_slots[pad].gamepad != nullptr;
        if (g_slots[pad].gamepad != nullptr) {
            ReadGamepad(g_slots[pad].gamepad, state);
        }
        g_device[pad] = state;
        if (pad == 0 && g_slots[0].gamepad != nullptr && (state.buttons != 0 || Deflected(state.left_x) || Deflected(state.left_y) ||
                                                           Deflected(state.right_x) || Deflected(state.right_y))) {
            g_last_keyboard = false;
        }
        Compose(pad);
    }
    ReadTouchpad(g_slots[0].gamepad);
    PollHostActions();
}

void InputHandleEvent(const SDL_Event &event) {
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        (event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.xrel != 0.0f || event.motion.yrel != 0.0f))) {
        g_last_keyboard = true;
    }
    if (MouseHandleEvent(event)) {
        return;
    }
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (event.key.scancode < SDL_SCANCODE_COUNT) {
                g_keys[event.key.scancode] = event.key.down;
            }
            if (event.key.down && !event.key.repeat) {
                EnsureBindings();
                for (std::size_t host = 0; host < kHostActionCount; ++host) {
                    if (KeyBound(host, event.key.scancode)) {
                        if (host == kZoomResetHost && (!g_mouse_zoom || g_menu_mouse)) {
                            continue;
                        }
                        ++g_host_presses[host];
                    }
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            // Preserve a click released before the next poll; don't count its poll edge twice.
            EnsureBindings();
            if (g_mouse_zoom && !g_menu_mouse &&
                std::ranges::any_of(g_bindings[kFirstHostAction + kZoomResetHost], [&](const Source &source) {
                    // SDL numbers middle 2/right 3; bindings use right Mouse2/middle Mouse3.
                    int button = event.button.button == SDL_BUTTON_MIDDLE ? 3 : event.button.button == SDL_BUTTON_RIGHT ? 2
                                                                                                                        : event.button.button;
                    return source.kind == Source::MouseButton && source.code == button;
                })) {
                ++g_host_presses[kZoomResetHost];
            }
            g_host_polled[kZoomResetHost] = HostPolledHeld(kZoomResetHost);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            g_host_polled[kZoomResetHost] = HostPolledHeld(kZoomResetHost);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_keys.fill(false);
            g_mouse_look.yaw = g_mouse_look.pitch = 0.0f;
            g_mouse_look.zoom = 0.0f;
            g_mouse_look.zoom_reset = false;
            g_scripted_wheel = 0.0f;
            g_host_presses[kZoomResetHost] = 0;
            g_menu_dx = 0.0f;
            g_menu_dy = 0.0f;
            break;
        default:
            break;
    }
}

void InputLatchPad(int pad) {
    if (pad != 0) {
        return;
    }
    g_stick_live = g_stick_read_serial == g_latch_serial;
    ++g_latch_serial;
    // Motion since the previous read, per tick: a frame that spans several ticks (or a load
    // between reads) turns the camera at the mouse's speed instead of all at once, and a mouse that
    // stopped reads as centred at the next read.
    std::int64_t now = ClockTickCount();
    std::int64_t ticks = g_last_latch_tick < 0 ? 1 : std::max<std::int64_t>(1, now - g_last_latch_tick);
    g_last_latch_tick = now;
    float dx = 0.0f;
    float dy = 0.0f;
    MouseTakeMotion(dx, dy);
    if (g_menu_mouse) {
        g_menu_dx += dx;
        g_menu_dy += dy;
        dx = 0.0f;
        dy = 0.0f;
    }
    g_mouse_dx = dx / static_cast<float>(ticks);
    g_mouse_dy = dy / static_cast<float>(ticks);
    // The look takes the whole motion rather than a per-tick speed, so the camera turns by what the
    // mouse moved, once, however the reads fall against the display's frames.
    if (g_override[0] && !g_menu_mouse) {
        dx = g_scripted.mouse_dx;
        dy = g_scripted.mouse_dy;
    } else if (static_cast<double>(ticks) > kMouseLookMaxGapSeconds * ClockTickRate()) {
        dx = 0.0f;
        dy = 0.0f;
    }
    std::uint64_t read = g_mouse_look.read + 1;
    g_mouse_look = MouseLookFromMotion(dx, dy);
    g_mouse_look.read = read;
    // Menus take the live wheel themselves. Everything else drains it each pad read, including
    // disabled zoom and loads, so it cannot be played back when gameplay next owns the camera.
    float wheel = g_menu_mouse ? 0.0f : MouseTakeWheel() + std::exchange(g_scripted_wheel, 0.0f);
    bool  reset = g_host_presses[kZoomResetHost] != 0;
    g_host_presses[kZoomResetHost] = 0;
    if (g_mouse_zoom && !g_menu_mouse &&
        static_cast<double>(ticks) <= kMouseLookMaxGapSeconds * ClockTickRate()) {
        g_mouse_look.zoom = std::isfinite(wheel) ? wheel : 0.0f;
        g_mouse_look.zoom_reset = reset;
    }
    Compose(0);
}

void InputSetMenuMouse(bool on) {
    g_menu_mouse = on;
    g_menu_dx = 0.0f;
    g_menu_dy = 0.0f;
    g_touch_dx = g_touch_dy = g_touch_wheel = 0.0f;
    g_touch_tap = 0;
    float dx = 0.0f;
    float dy = 0.0f;
    MouseTakeMotion(dx, dy);
    g_mouse_dx = 0.0f;
    g_mouse_dy = 0.0f;
    g_mouse_look.yaw = g_mouse_look.pitch = 0.0f;
    g_mouse_look.zoom = 0.0f;
    g_mouse_look.zoom_reset = false;
    g_scripted_wheel = 0.0f;
    g_host_presses[kZoomResetHost] = 0;
    g_host_polled[kZoomResetHost] = HostPolledHeld(kZoomResetHost);
    MouseTakeWheel();
    Compose(0);
}

void InputSetMenuNavigation(bool on) {
    g_menu_navigation = on;
}

void InputSetDeveloperMenu(bool on) {
    g_developer_menu = on;
}

bool InputDeveloperMenu() {
    return g_developer_menu;
}

InputMenuMouse InputTakeMenuMouse() {
    float dx = 0.0f;
    float dy = 0.0f;
    MouseTakeMotion(dx, dy);
    InputMenuMouse mouse;
    // A script's motion is per tick, and a menu takes once a tick.
    mouse.dx = g_menu_dx + dx + g_scripted.mouse_dx + std::exchange(g_touch_dx, 0.0f);
    mouse.dy = g_menu_dy + dy + g_scripted.mouse_dy + std::exchange(g_touch_dy, 0.0f);
    mouse.wheel = MouseTakeWheel() + std::exchange(g_scripted_wheel, 0.0f) + std::exchange(g_touch_wheel, 0.0f);
    mouse.buttons = MouseButtons() | g_scripted.mouse_buttons | g_touch_held;
    // A tap is a click the pointer sees pressed on one take and let go on the next.
    if (g_touch_tap != 0) {
        if (g_touch_tap_shown) {
            g_touch_tap = 0;
        } else {
            mouse.buttons |= g_touch_tap;
            g_touch_tap_shown = true;
        }
    }
    g_menu_dx = 0.0f;
    g_menu_dy = 0.0f;
    return mouse;
}

const InputMouseLook &InputGetMouseLook() { return g_mouse_look; }

void InputNoteLeftStickRead() { g_stick_read_serial = g_latch_serial; }

bool InputLeftStickLive() { return g_stick_live; }

const InputPadState &InputGetPad(int pad) {
    static const InputPadState kAbsent;
    if (pad < 0 || pad >= kInputPadCount) {
        return kAbsent;
    }
    g_view[pad] = g_override[pad] ? *g_override[pad] : g_state[pad];
    if (!g_stick_live) {
        g_view[pad].buttons |= g_view[pad].stick_dpad;
    }
    return g_view[pad];
}

void InputSetRumble(int pad, InputRumble rumble) {
    if (pad < 0 || pad >= kInputPadCount) {
        return;
    }
    PadSlot &slot = g_slots[pad];
    bool     changed = slot.rumble.small_motor != rumble.small_motor || slot.rumble.large_motor != rumble.large_motor;
    bool     active = rumble.small_motor || rumble.large_motor != 0;
    slot.rumble = rumble;
    if (changed || (active && SDL_GetTicks() - slot.rumble_time >= kRumbleMilliseconds / 2)) {
        SendRumble(pad);
    }
}

void InputSetLightbar(int pad, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    if (pad < 0 || pad >= kInputPadCount) {
        return;
    }
    PadSlot &slot = g_slots[pad];
    if (slot.led_wanted && slot.led[0] == red && slot.led[1] == green && slot.led[2] == blue) {
        return;
    }
    slot.led_wanted = true;
    slot.led[0] = red;
    slot.led[1] = green;
    slot.led[2] = blue;
    ApplyLightbar(slot);
}

InputRumble InputGetRumble(int pad) {
    if (pad < 0 || pad >= kInputPadCount) {
        return {};
    }
    return g_slots[pad].rumble;
}

void InputSetOverride(int pad, const InputPadState *state) {
    if (pad < 0 || pad >= kInputPadCount) {
        return;
    }
    if (state != nullptr) {
        g_override[pad] = *state;
    } else {
        g_override[pad].reset();
    }
}

InputPadState InputApplyKeyboardMouse(InputPadState base, const InputKeyboardMouse &held) {
    EnsureBindings();
    std::array<int, 4>   push{};
    std::array<float, 4> mouse{};
    float                mouse_y = g_mouse.invert_y ? -held.mouse_dy : held.mouse_dy;
    for (std::size_t i = 0; i < kActionCount; ++i) {
        bool down = false;
        for (const Source &source : g_bindings[i]) {
            if (kActions[i].kind != ActionKind::Host && ZoomReserved(source)) {
                continue;
            }
            switch (source.kind) {
                case Source::Key:
                    down |= std::ranges::find(held.keys, source.code) != held.keys.end();
                    break;
                case Source::MouseButton:
                    down |= !g_menu_mouse && (held.mouse_buttons & (1u << (source.code - 1))) != 0;
                    break;
                case Source::MouseAxis:
                    if (g_menu_mouse) {
                        break;
                    }
                    mouse[kActions[i].axis] += source.scale * MouseRadiansPerCount() / kMouseStickRadians *
                                               (source.code == 0 ? held.mouse_dx : mouse_y);
                    break;
                case Source::GamepadButton:
                    break;
            }
        }
        if (!down) {
            continue;
        }
        if (kActions[i].kind == ActionKind::Button) {
            base.buttons |= kActions[i].button;
        } else if (kActions[i].kind == ActionKind::HalfAxis) {
            push[kActions[i].axis] += kActions[i].direction;
        }
    }
    if (std::ranges::contains(held.keys, SDL_SCANCODE_ESCAPE)) {
        base.buttons |= g_menu_navigation ? kInputCircle : kInputTriangle;
    }
    std::array<float, 4> deflection{};
    for (int x = kAxisLeftX; x <= kAxisRightX; x += 2) {
        float key_x = static_cast<float>((push[x] > 0) - (push[x] < 0));
        float key_y = static_cast<float>((push[x + 1] > 0) - (push[x + 1] < 0));
        // Two keys held make a diagonal of the same length as one, as a stick's gate does.
        if (key_x != 0.0f && key_y != 0.0f) {
            key_x *= std::numbers::sqrt2_v<float> / 2.0f;
            key_y *= std::numbers::sqrt2_v<float> / 2.0f;
        }
        deflection[x] = std::clamp(key_x + mouse[x], -1.0f, 1.0f);
        deflection[x + 1] = std::clamp(key_y + mouse[x + 1], -1.0f, 1.0f);
    }
    if (push[kAxisLeftY] < 0) {
        base.stick_dpad |= kInputUp;
    } else if (push[kAxisLeftY] > 0) {
        base.stick_dpad |= kInputDown;
    }
    if (push[kAxisLeftX] < 0) {
        base.stick_dpad |= kInputLeft;
    } else if (push[kAxisLeftX] > 0) {
        base.stick_dpad |= kInputRight;
    }
    for (int axis = kAxisLeftX; axis <= kAxisRightY; ++axis) {
        std::uint8_t &byte = AxisOf(base, static_cast<Axis>(axis));
        if (!Deflected(byte) && deflection[axis] != 0.0f) {
            byte = InputStickByte(deflection[axis]);
        }
    }
    return base;
}

std::uint8_t InputStickByte(float deflection) {
    if (!std::isfinite(deflection)) {
        return kInputAxisCentre;
    }
    float target = std::min(std::fabs(deflection), 1.0f) * kGameAxisRange;
    if (target < 0.5f) {
        return kInputAxisCentre;
    }
    // The smallest step past the dead zone that the game's integer scaling reads as at least target.
    int step = static_cast<int>(std::ceil(target * kLiveSpan / kGameAxisRange - 1e-3f));
    step = std::clamp(step, 1, kLiveSpan);
    if (deflection > 0.0f) {
        return static_cast<std::uint8_t>(std::min(255, kInputAxisCentre + kDeadZoneAbove + step));
    }
    return static_cast<std::uint8_t>(std::max(0, kInputAxisCentre - kDeadZoneBelow - step));
}

bool InputBindKeys(std::string_view action, std::span<const std::string_view> keys) {
    EnsureBindings();
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (kActions[i].name != action) {
            continue;
        }
        std::vector<Source> sources;
        if (!ParseSources(keys, kActions[i].kind, sources)) {
            return false;
        }
        g_bindings[i] = std::move(sources);
        return true;
    }
    return false;
}

std::span<const InputActionInfo> InputActions() {
    static const std::vector<InputActionInfo> actions = [] {
        std::vector<InputActionInfo> list;
        list.reserve(kActionCount);
        for (std::size_t i = 0; i < kActionCount; ++i) {
            list.push_back({kActions[i].name, i >= kFirstHostAction, kActions[i].kind == ActionKind::Axis});
        }
        return list;
    }();
    return actions;
}

bool InputActionTakesGamepad(std::string_view action) {
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (kActions[i].name == action) {
            return i >= kFirstHostAction;
        }
    }
    return false;
}

std::string InputBindingLabel(std::string_view action) {
    EnsureBindings();
    std::string text;
    bool        known = false;
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (kActions[i].name != action) {
            continue;
        }
        known = true;
        for (const Source &source : g_bindings[i]) {
            std::string one;
            switch (source.kind) {
                case Source::Key: {
                    const char *name = SDL_GetScancodeName(static_cast<SDL_Scancode>(source.code));
                    one = name != nullptr ? name : "";
                    break;
                }
                case Source::MouseButton:
                    one = "Mouse" + std::to_string(source.code);
                    break;
                case Source::MouseAxis:
                    one = std::string(source.scale < 0.0f ? "-" : "") + (source.code == 0 ? "MouseX" : "MouseY");
                    break;
                case Source::GamepadButton: {
                    const char *name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(source.code));
                    one = name != nullptr ? std::string("Gamepad:") + name : "";
                    break;
                }
            }
            if (!one.empty()) {
                if (!text.empty()) {
                    text += ", ";
                }
                text += one;
            }
        }
    }
    if (!known) {
        return {};
    }
    return text.empty() ? "None" : text;
}

void InputBeginBindCapture() {
    EnsureBindings();
    g_bind_capture = true;
    SnapshotBind();
}

std::string InputTakeBindKey(bool allow_gamepad) {
    if (!g_bind_capture) {
        return {};
    }
    std::string found;
    for (int scancode = 0; scancode < SDL_SCANCODE_COUNT && found.empty(); ++scancode) {
        if (!BindKeyHeld(scancode) || g_bind_keys[scancode]) {
            continue;
        }
        const char *name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
        if (name != nullptr && *name != '\0') {
            found = name;
        }
    }
    std::uint32_t mouse = MouseButtons() | g_scripted.mouse_buttons;
    for (int bit = 0; bit < kMouseButtonCount && found.empty(); ++bit) {
        if ((mouse & (1u << bit)) != 0 && (g_bind_mouse & (1u << bit)) == 0) {
            found = "Mouse" + std::to_string(bit + 1);
        }
    }
    if (allow_gamepad && found.empty()) {
        std::uint16_t pad = BindGamepadHeld();
        for (std::size_t i = 0; i < std::size(kGamepadButtons) && found.empty(); ++i) {
            if ((pad & (1u << i)) != 0 && (g_bind_pad & (1u << i)) == 0) {
                const char *name = SDL_GetGamepadStringForButton(kGamepadButtons[i]);
                if (name != nullptr && *name != '\0') {
                    found = std::string("Gamepad:") + name;
                }
            }
        }
    }
    SnapshotBind();
    return found;
}

bool InputBindSourceHeld(std::string_view name) {
    std::string lower = Lower(name);
    if (lower.size() == 6 && lower.starts_with("mouse") && lower[5] >= '1' && lower[5] < '1' + kMouseButtonCount) {
        return ((MouseButtons() | g_scripted.mouse_buttons) & (1u << (lower[5] - '1'))) != 0;
    }
    if (lower.starts_with("gamepad:")) {
        SDL_GamepadButton button = SDL_GetGamepadButtonFromString(lower.substr(8).c_str());
        if (button == SDL_GAMEPAD_BUTTON_INVALID) {
            return false;
        }
        std::uint16_t held = BindGamepadHeld();
        for (std::size_t i = 0; i < std::size(kGamepadButtons); ++i) {
            if (kGamepadButtons[i] == button) {
                return (held & (1u << i)) != 0;
            }
        }
        return false;
    }
    int scancode = InputScancodeFromName(name);
    return scancode >= 0 && BindKeyHeld(scancode);
}

InputGlyphFamily InputActiveGlyphFamily() {
    return g_last_keyboard ? InputGlyphFamily::Keyboard : g_pad_family;
}

std::string InputPrimaryBindingName(std::string_view action) {
    EnsureBindings();
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (kActions[i].name != action) {
            continue;
        }
        for (const Source &source : g_bindings[i]) {
            if (source.kind == Source::Key) {
                std::string name;
                for (const char *c = SDL_GetScancodeName(static_cast<SDL_Scancode>(source.code)); *c; ++c) {
                    if (*c != ' ') {
                        name += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
                    }
                }
                return name;
            }
            if (source.kind == Source::MouseButton) {
                return "mouse" + std::to_string(source.code);
            }
        }
        return {};
    }
    return {};
}

void InputResetBindings() {
    g_bindings_ready = false;
    EnsureBindings();
}

void InputSetMouseSettings(const InputMouseSettings &settings) {
    EnsureBindings();
    g_mouse = settings;
    MouseConfigure(settings.capture, settings.release_scancodes);
}

const InputMouseSettings &InputGetMouseSettings() {
    EnsureBindings();
    return g_mouse;
}

int InputScancodeFromName(std::string_view name) {
    std::string lower = Lower(name);
    if (lower == "grave" || lower == "backquote" || lower == "backtick") {
        return SDL_SCANCODE_GRAVE;
    }
    std::string  text(name);
    SDL_Scancode scancode = SDL_GetScancodeFromName(text.c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        std::ranges::replace(text, '_', ' ');
        scancode = SDL_GetScancodeFromName(text.c_str());
    }
    return scancode == SDL_SCANCODE_UNKNOWN ? -1 : static_cast<int>(scancode);
}

bool InputHostHeld(InputHostAction action) {
    EnsureBindings();
    std::size_t host = static_cast<std::size_t>(action);
    for (const Source &source : g_bindings[kFirstHostAction + host]) {
        if (source.kind == Source::Key && g_keys[source.code]) {
            return true;
        }
    }
    return HostPolledHeld(host);
}

void InputSetLookOnLeftStick(bool left) {
    g_look_left = left;
}

bool InputHostPressed(InputHostAction action) {
    int &presses = g_host_presses[static_cast<std::size_t>(action)];
    if (presses == 0) {
        return false;
    }
    --presses;
    return true;
}

void InputSetScriptedDevices(const InputKeyboardMouse &held) {
    EnsureBindings();
    g_scripted = held;
    g_scripted_wheel = held.mouse_wheel;
    PollHostActions();
}

static bool g_movement_locked = false;

void InputSetMovementLocked(bool locked) { g_movement_locked = locked; }

InputKeyboardMovement InputGetKeyboardMovement() {
    EnsureBindings();
    if (g_menu_mouse || g_movement_locked) {
        return {};
    }
    InputKeyboardMouse held = g_override[0] ? g_scripted : LiveKeyboardMouse();
    int                x = 0, y = 0;
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const auto &action = kActions[i];
        if (action.kind != ActionKind::HalfAxis || action.axis > kAxisLeftY) {
            continue;
        }
        bool down = std::ranges::any_of(g_bindings[i], [&](const Source &source) {
            return source.kind == Source::Key && !ZoomReserved(source) && std::ranges::find(held.keys, source.code) != held.keys.end();
        });
        if (down) {
            (action.axis == kAxisLeftX ? x : y) += action.direction;
        }
    }
    InputKeyboardMovement move{float((x > 0) - (x < 0)), float((y > 0) - (y < 0))};
    if (move.x && move.y) {
        move.x *= std::numbers::sqrt2_v<float> / 2;
        move.y *= std::numbers::sqrt2_v<float> / 2;
    }
    if (move.x || move.y) {
        InputNoteLeftStickRead();
    }
    return move;
}
