#pragma once

#include "gfx/gfx.hpp"

// The button symbols the game draws in its text and menus, redrawn from glyphs/ (beside the executable,
// or in the save folder): one atlas per style (PS4, PS5, Xbox, Switch, keyboard and mouse) and
// glyphs.json with each symbol's rectangle. Host side: plain types only. See docs/GLYPHS.md.
namespace glyphs {

// What the PS2 pad's buttons are called in the game.
enum class Button {
    Cross,
    Circle,
    Square,
    Triangle,
    L1,
    R1,
    L2,
    R2,
    Start,
    Select,
    Dpad,
    DpadUpDown,
    DpadSides,
    Up,
    Down,
    Left,
    Right,
    L3,
    R3,
    LStick,
    RStick,
};

// One symbol: where it is in its atlas (texels of the atlas, normalised by the renderer as for any
// Draw2D texture) and how big it is. width and height are in reference pixels, where
// kReference is the width of a round face button; a caller scales by its own slot.
struct Image {
    gfx::TextureBinding binding;
    float               u0 = 0.0f;
    float               v0 = 0.0f;
    float               u1 = 0.0f;
    float               v1 = 0.0f;
    float               width = 0.0f;
    float               height = 0.0f;
    // A keycap or mouse of the keyboard style: wider or narrower than a round button, and drawn bigger.
    bool key = false;
};

constexpr float kReference = 64.0f;

// The symbol for a pad button under input.glyphs and input.glyph_device: the style of the forced
// device or, on Auto, of the device last used (the key a keyboard binds, for the keyboard style).
// False when the game's own art should be drawn: the setting is "original", or the files are not
// there, or the style has no symbol for it.
bool Find(Button button, Image &out);

// The same for an explicit style name ("ps4", "ps5", "xbox", "switch", "keyboard") and glyph name as
// glyphs.json spells them ("cross", "l1", "dpad", "space", "mouse1"...); for the tests and tools.
bool FindNamed(const char *style, const char *name, Image &out);

// Forgets what was loaded (and frees the atlases), so the next Find reads glyphs/ again.
void Reset();

} // namespace glyphs
