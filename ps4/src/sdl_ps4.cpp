// The SDL 3 calls the shared input code makes, answered for the PS4 over scePad. The real SDL 3 headers
// supply the types; nothing else of SDL is linked on the console.

#ifdef __PS4__

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <orbis/Pad.h>

#include "ps4_platform.hpp"

namespace {

// One DualShock 4 as one SDL gamepad. SDL_UpdateGamepads polls it; every getter reads that snapshot.
constexpr SDL_JoystickID kPadId = 1;

ps4::PadState g_pad;
Uint64        g_rumble_until = 0;
SDL_Gamepad  *const kGamepad = reinterpret_cast<SDL_Gamepad *>(&g_pad);

Sint16 StickAxis(uint8_t value) {
    // 0..255 with 128 at rest, to -32768..32767.
    int scaled = (static_cast<int>(value) - 128) * 258;
    return static_cast<Sint16>(scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : scaled);
}

Sint16 TriggerAxis(uint8_t value) {
    // SDL 3 triggers run 0..32767.
    return static_cast<Sint16>(static_cast<int>(value) * 32767 / 255);
}

struct ButtonName {
    SDL_GamepadButton button;
    const char       *name;
};

constexpr ButtonName kButtonNames[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, "a"},           {SDL_GAMEPAD_BUTTON_EAST, "b"},
    {SDL_GAMEPAD_BUTTON_WEST, "x"},            {SDL_GAMEPAD_BUTTON_NORTH, "y"},
    {SDL_GAMEPAD_BUTTON_BACK, "back"},         {SDL_GAMEPAD_BUTTON_GUIDE, "guide"},
    {SDL_GAMEPAD_BUTTON_START, "start"},       {SDL_GAMEPAD_BUTTON_LEFT_STICK, "leftstick"},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "rightstick"}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "leftshoulder"},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "rightshoulder"}, {SDL_GAMEPAD_BUTTON_DPAD_UP, "dpup"},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "dpdown"},  {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "dpleft"},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "dpright"}, {SDL_GAMEPAD_BUTTON_MISC1, "misc1"},
    {SDL_GAMEPAD_BUTTON_TOUCHPAD, "touchpad"},
};

} // namespace

// ---- Generic ---------------------------------------------------------------------------------

bool SDL_InitSubSystem(SDL_InitFlags) { return true; }

void SDL_QuitSubSystem(SDL_InitFlags) {}

const char *SDL_GetError(void) { return ""; }

void SDL_free(void *memory) { free(memory); }

Uint64 SDL_GetTicks(void) {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<Uint64>(now.tv_sec) * 1000u + static_cast<Uint64>(now.tv_nsec) / 1000000u;
}

bool SDL_SetHintWithPriority(const char *, const char *, SDL_HintPriority) { return true; }

bool SDL_PushEvent(SDL_Event *) { return true; }

// ---- Window and mouse: there is neither ------------------------------------------------------

SDL_WindowID SDL_GetWindowID(SDL_Window *) { return 1; }

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *) { return SDL_WINDOW_FULLSCREEN | SDL_WINDOW_INPUT_FOCUS; }

bool SDL_SetWindowRelativeMouseMode(SDL_Window *, bool) { return true; }

bool SDL_GetWindowRelativeMouseMode(SDL_Window *) { return false; }

bool SDL_GetBooleanProperty(SDL_PropertiesID, const char *name, bool default_value) {
    // The pad's light bar is RGB: the only property asked of the gamepad.
    if (name != nullptr && strcmp(name, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN) == 0) {
        return true;
    }
    return default_value;
}

const char *SDL_GetScancodeName(SDL_Scancode) { return ""; }

SDL_Scancode SDL_GetScancodeFromName(const char *) { return SDL_SCANCODE_UNKNOWN; }

// ---- Gamepad ----------------------------------------------------------------------------------

void SDL_UpdateGamepads(void) {
    ps4::ReadPad(g_pad);
    if (g_rumble_until != 0 && SDL_GetTicks() >= g_rumble_until) {
        g_rumble_until = 0;
        ps4::SetPadVibration(0, 0);
    }
}

SDL_JoystickID *SDL_GetGamepads(int *count) {
    ps4::ReadPad(g_pad);
    const int found = g_pad.connected ? 1 : 0;
    if (count != nullptr) {
        *count = found;
    }
    auto *ids = static_cast<SDL_JoystickID *>(malloc(sizeof(SDL_JoystickID) * (found + 1)));
    if (ids == nullptr) {
        return nullptr;
    }
    if (found) {
        ids[0] = kPadId;
    }
    ids[found] = 0;
    return ids;
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) { return id == kPadId ? kGamepad : nullptr; }

void SDL_CloseGamepad(SDL_Gamepad *) {}

bool SDL_GamepadConnected(SDL_Gamepad *) { return g_pad.connected; }

SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad *) { return kPadId; }

SDL_PropertiesID SDL_GetGamepadProperties(SDL_Gamepad *) { return 1; }

SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *) { return SDL_GAMEPAD_TYPE_PS4; }

Uint16 SDL_GetGamepadVendor(SDL_Gamepad *) { return 0x054C; }

Uint16 SDL_GetGamepadProduct(SDL_Gamepad *) { return 0x09CC; }

bool SDL_GetGamepadButton(SDL_Gamepad *, SDL_GamepadButton button) {
    const uint32_t b = g_pad.buttons;
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return b & ORBIS_PAD_BUTTON_CROSS;
        case SDL_GAMEPAD_BUTTON_EAST:
            return b & ORBIS_PAD_BUTTON_CIRCLE;
        case SDL_GAMEPAD_BUTTON_WEST:
            return b & ORBIS_PAD_BUTTON_SQUARE;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return b & ORBIS_PAD_BUTTON_TRIANGLE;
        // The SHARE button belongs to the system and never reaches a game. The touch pad's click is SDL's
        // touchpad button, which the port's PS4 rules (platform/gamepad_map.hpp) make the game's Select.
        case SDL_GAMEPAD_BUTTON_BACK:
            return false;
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:
            return b & ORBIS_PAD_BUTTON_TOUCH_PAD;
        case SDL_GAMEPAD_BUTTON_START:
            return b & ORBIS_PAD_BUTTON_OPTIONS;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return b & ORBIS_PAD_BUTTON_L3;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return b & ORBIS_PAD_BUTTON_R3;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return b & ORBIS_PAD_BUTTON_L1;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return b & ORBIS_PAD_BUTTON_R1;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return b & ORBIS_PAD_BUTTON_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return b & ORBIS_PAD_BUTTON_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return b & ORBIS_PAD_BUTTON_LEFT;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return b & ORBIS_PAD_BUTTON_RIGHT;
        default:
            return false;
    }
}

Sint16 SDL_GetGamepadAxis(SDL_Gamepad *, SDL_GamepadAxis axis) {
    switch (axis) {
        case SDL_GAMEPAD_AXIS_LEFTX:
            return StickAxis(g_pad.left_x);
        case SDL_GAMEPAD_AXIS_LEFTY:
            return StickAxis(g_pad.left_y);
        case SDL_GAMEPAD_AXIS_RIGHTX:
            return StickAxis(g_pad.right_x);
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return StickAxis(g_pad.right_y);
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return TriggerAxis(g_pad.l2);
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return TriggerAxis(g_pad.r2);
        default:
            return 0;
    }
}

const char *SDL_GetGamepadStringForButton(SDL_GamepadButton button) {
    for (const ButtonName &entry : kButtonNames) {
        if (entry.button == button) {
            return entry.name;
        }
    }
    return nullptr;
}

SDL_GamepadButton SDL_GetGamepadButtonFromString(const char *name) {
    if (name != nullptr) {
        for (const ButtonName &entry : kButtonNames) {
            if (strcmp(entry.name, name) == 0) {
                return entry.button;
            }
        }
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

bool SDL_RumbleGamepad(SDL_Gamepad *, Uint16 low_frequency, Uint16 high_frequency, Uint32 duration_ms) {
    ps4::SetPadVibration(static_cast<uint8_t>(low_frequency >> 8), static_cast<uint8_t>(high_frequency >> 8));
    g_rumble_until = (low_frequency | high_frequency) != 0 && duration_ms != 0 ? SDL_GetTicks() + duration_ms : 0;
    return true;
}

bool SDL_SetGamepadLED(SDL_Gamepad *, Uint8 red, Uint8 green, Uint8 blue) {
    ps4::SetPadLight(red, green, blue);
    return true;
}

bool SDL_SetGamepadPlayerIndex(SDL_Gamepad *, int) { return true; }

bool SDL_GamepadHasSensor(SDL_Gamepad *, SDL_SensorType type) {
    return type == SDL_SENSOR_GYRO || type == SDL_SENSOR_ACCEL;
}

bool SDL_SetGamepadSensorEnabled(SDL_Gamepad *, SDL_SensorType type, bool) {
    return type == SDL_SENSOR_GYRO || type == SDL_SENSOR_ACCEL;
}

bool SDL_GetGamepadSensorData(SDL_Gamepad *, SDL_SensorType type, float *data, int num_values) {
    const float *source = type == SDL_SENSOR_GYRO ? g_pad.gyro : type == SDL_SENSOR_ACCEL ? g_pad.accel : nullptr;
    if (source == nullptr || data == nullptr) {
        return false;
    }
    for (int i = 0; i < num_values; i++) {
        data[i] = i < 3 ? source[i] : 0.0f;
    }
    return true;
}

int SDL_GetNumGamepadTouchpads(SDL_Gamepad *) { return 1; }

int SDL_GetNumGamepadTouchpadFingers(SDL_Gamepad *, int touchpad) { return touchpad == 0 ? 2 : 0; }

bool SDL_GetGamepadTouchpadFinger(SDL_Gamepad *, int touchpad, int finger, bool *down, float *x, float *y,
                                  float *pressure) {
    if (touchpad != 0 || finger < 0 || finger > 1) {
        return false;
    }
    if (down != nullptr) {
        *down = g_pad.touch_down[finger];
    }
    if (x != nullptr) {
        *x = static_cast<float>(g_pad.touch_x[finger]) / 1919.0f;
    }
    if (y != nullptr) {
        *y = static_cast<float>(g_pad.touch_y[finger]) / 941.0f;
    }
    if (pressure != nullptr) {
        *pressure = g_pad.touch_down[finger] ? 1.0f : 0.0f;
    }
    return true;
}

#endif // __PS4__
