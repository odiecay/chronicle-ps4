#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>

#include "platform/gamepad_map.hpp"

// A DualShock 2 puts Select where a DualShock 4 or DualSense has its Share/Create button, but those
// pads report the touchpad click apart from it, and players reach for the touchpad as Select
//. So on PS4 and PS5 pads the touchpad click presses Select and Share presses nothing;
// every other pad keeps Back/View as Select.

TEST(PlatformGamepadMap, PlayStationMovesSelectToTheTouchpad) {
    for (SDL_GamepadType type : {SDL_GAMEPAD_TYPE_PS4, SDL_GAMEPAD_TYPE_PS5}) {
        EXPECT_EQ(GamepadButtonPad(type, SDL_GAMEPAD_BUTTON_TOUCHPAD), kInputSelect);
        EXPECT_EQ(GamepadButtonPad(type, SDL_GAMEPAD_BUTTON_BACK), 0);
    }
}

TEST(PlatformGamepadMap, OtherPadsKeepBackAsSelect) {
    const std::array types = {SDL_GAMEPAD_TYPE_UNKNOWN, SDL_GAMEPAD_TYPE_STANDARD,
                              SDL_GAMEPAD_TYPE_XBOX360, SDL_GAMEPAD_TYPE_XBOXONE,
                              SDL_GAMEPAD_TYPE_PS3, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO};
    for (SDL_GamepadType type : types) {
        EXPECT_EQ(GamepadButtonPad(type, SDL_GAMEPAD_BUTTON_BACK), kInputSelect);
        EXPECT_EQ(GamepadButtonPad(type, SDL_GAMEPAD_BUTTON_TOUCHPAD), 0);
    }
}

TEST(PlatformGamepadMap, OtherButtonsKeepTheDualShockLayout) {
    const std::pair<SDL_GamepadButton, std::uint16_t> pairs[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH,          kInputCross   },
        {SDL_GAMEPAD_BUTTON_EAST,           kInputCircle  },
        {SDL_GAMEPAD_BUTTON_WEST,           kInputSquare  },
        {SDL_GAMEPAD_BUTTON_NORTH,          kInputTriangle},
        {SDL_GAMEPAD_BUTTON_START,          kInputStart   },
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,     kInputL3      },
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK,    kInputR3      },
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  kInputL1      },
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, kInputR1      },
        {SDL_GAMEPAD_BUTTON_DPAD_UP,        kInputUp      },
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN,      kInputDown    },
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,      kInputLeft    },
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     kInputRight   },
    };
    for (const auto &[button, pad] : pairs) {
        EXPECT_EQ(GamepadButtonPad(SDL_GAMEPAD_TYPE_PS5, button), pad);
        EXPECT_EQ(GamepadButtonPad(SDL_GAMEPAD_TYPE_XBOX360, button), pad);
    }
}
