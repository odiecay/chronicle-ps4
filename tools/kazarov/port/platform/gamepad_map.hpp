#pragma once

#include <SDL3/SDL_gamepad.h>

#include <cstdint>

#include "input.hpp"

// Which DualShock 2 button (a kInput*) a gamepad button presses, for a pad SDL reports as `type`,
// or 0 for a button that presses nothing. The button list is in input.cpp; this keeps the mapping
// a pure function so a test can check every SDL gamepad type without a pad attached.
//
// A DualShock 2's Select sits where a DualShock 4 or DualSense has its Share/Create button, but
// those pads report the touchpad click apart from it (SDL_GAMEPAD_BUTTON_TOUCHPAD), and players
// reach for the touchpad as Select. So on PS4 and PS5 pads the touchpad click presses
// Select and Share is left unbound. Every other pad keeps Back/View as Select; a PS3 has no
// touchpad, so its Select stays on Select.
inline std::uint16_t GamepadButtonPad(SDL_GamepadType type, SDL_GamepadButton button) {
    bool touchpad_select = type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return kInputCross;
        case SDL_GAMEPAD_BUTTON_EAST:
            return kInputCircle;
        case SDL_GAMEPAD_BUTTON_WEST:
            return kInputSquare;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return kInputTriangle;
        case SDL_GAMEPAD_BUTTON_BACK:
            return touchpad_select ? 0 : kInputSelect;
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:
            return touchpad_select ? kInputSelect : 0;
        case SDL_GAMEPAD_BUTTON_START:
            return kInputStart;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return kInputL3;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return kInputR3;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return kInputL1;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return kInputR1;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return kInputUp;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return kInputDown;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return kInputLeft;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return kInputRight;
        default:
            return 0;
    }
}
