#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <SDL3/SDL_gamepad.h>

#include "platform/input.hpp"

// The gamepad families the button symbols are drawn with, and the key names the keyboard style looks
// them up by.

TEST(Glyphs, GamepadsAreDrawnWithTheirOwnSymbolsAndUnknownOnesAsXbox) {
    constexpr unsigned kSony = 0x054C, kMicrosoft = 0x045E, kValve = 0x28DE, kOther = 0x1234;
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_PS3, kSony, 0x0268), InputGlyphFamily::Ps3);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_PS4, kSony, 0x09CC), InputGlyphFamily::Ps4);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_PS5, kSony, 0x0CE6), InputGlyphFamily::Ps5);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_XBOXONE, kMicrosoft, 0x02EA), InputGlyphFamily::Xbox);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057E, 0x2009),
              InputGlyphFamily::Switch);
    // Valve's pads: SDL may call them standard; the product tells the Deck from the Controllers.
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_STANDARD, kValve, 0x1205), InputGlyphFamily::SteamDeck);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_STANDARD, kValve, 0x1102), InputGlyphFamily::SteamController);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_UNKNOWN, kValve, 0x1302), InputGlyphFamily::SteamController);
    // Anything else SDL does not name is an Xbox-layout pad.
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_STANDARD, kOther, 1), InputGlyphFamily::Xbox);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_UNKNOWN, 0, 0), InputGlyphFamily::Xbox);
}

TEST(Glyphs, BindingNamesAreTheGlyphTableSpelling) {
    InputResetBindings();
    ASSERT_EQ(InputPrimaryBindingName("cross"), "mouse1");
    ASSERT_EQ(InputPrimaryBindingName("circle"), "mouse2");
    ASSERT_EQ(InputPrimaryBindingName("square"), "e");
    ASSERT_EQ(InputPrimaryBindingName("triangle"), "tab");
    ASSERT_EQ(InputPrimaryBindingName("start"), "return");
    ASSERT_EQ(InputPrimaryBindingName("select"), "backspace");
    ASSERT_EQ(InputPrimaryBindingName("up"), "up");
    ASSERT_EQ(InputPrimaryBindingName("nonsense"), "");
    const std::string_view keys[] = {"Left Ctrl"};
    ASSERT_TRUE(InputBindKeys("cross", keys));
    ASSERT_EQ(InputPrimaryBindingName("cross"), "leftctrl");
    InputResetBindings();
}
