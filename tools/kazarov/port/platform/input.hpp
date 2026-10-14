#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct HostSettings;
union SDL_Event;

// Host input shaped like two DualShock 2 controllers. The first gamepad, the keyboard and the mouse
// drive pad 0, the second gamepad pad 1.

constexpr int kInputPadCount = 2;

// libpad's button word with its bytes in the order the game reads them
// (`((data[2] << 8) | data[3]) ^ 0xFFFF`), active-high: the game's PadButton.
enum InputButton : std::uint16_t {
    kInputL2 = 0x0001,
    kInputR2 = 0x0002,
    kInputL1 = 0x0004,
    kInputR1 = 0x0008,
    kInputTriangle = 0x0010,
    kInputCircle = 0x0020,
    kInputCross = 0x0040,
    kInputSquare = 0x0080,
    kInputSelect = 0x0100,
    kInputL3 = 0x0200,
    kInputR3 = 0x0400,
    kInputStart = 0x0800,
    kInputUp = 0x1000,
    kInputRight = 0x2000,
    kInputDown = 0x4000,
    kInputLeft = 0x8000,
};

constexpr std::uint8_t kInputAxisCentre = 128;

struct InputPadState {
    bool          connected = false;
    std::uint16_t buttons = 0;
    std::uint8_t  left_x = kInputAxisCentre;
    std::uint8_t  left_y = kInputAxisCentre;
    std::uint8_t  right_x = kInputAxisCentre;
    std::uint8_t  right_y = kInputAxisCentre;
    // D-pad bits the left-stick keys add only while the game is not reading the left stick: the
    // screens that read the d-pad alone (the developer menu, the dungeon loader).
    std::uint16_t stick_dpad = 0;
};

// What the keyboard and the mouse hold: SDL scancodes, mouse buttons (bit n-1 for Mouse n) and the
// mouse motion over one tick in pixels, y growing downward.
struct InputKeyboardMouse {
    std::vector<int> keys;
    std::uint32_t    mouse_buttons = 0;
    float            mouse_dx = 0.0f;
    float            mouse_dy = 0.0f;
    float            mouse_wheel = 0.0f;
};

struct InputMouseSettings {
    // Degrees of camera turn per count of mouse motion.
    float            sensitivity = 0.1f;
    bool             invert_y = false;
    bool             capture = true;
    std::vector<int> release_scancodes;
};

struct InputRumble {
    bool         small_motor = false;
    std::uint8_t large_motor = 0;
};

// Opens SDL's gamepad subsystem, applies config.json's input section and watches the window's events.
// Input works without a gamepad subsystem or a window (nothing but overrides then).
void InputInit();

// Applies config's input section over the default bindings and mouse settings, as InputInit does
// with SettingsGet() and a change of settings does while the game runs.
void InputApplyConfig(const HostSettings &config);

void InputShutdown();

// Samples the gamepads, opening newly connected ones, and folds in the keyboard and mouse.
void InputPoll();

// The window event hook InputInit installs: keys, mouse buttons and motion, focus.
void InputHandleEvent(const SDL_Event &event);

// The game is about to read pad: the mouse motion since the previous read becomes this read's mouse
// look (and the deflection of a stick bound to the mouse), and whether the game read the left stick
// since then decides whether stick_dpad applies to it.
void InputLatchPad(int pad);

// How far the mouse turns the camera over one tick, in radians: the motion between the game's last
// two reads of pad 0 at input.mouse_sensitivity, the script's motion while one drives the pad. yaw
// above zero turns the view right, pitch above zero looks up. An axis a stick binding takes the
// mouse for (MouseX, MouseY) reads zero. Motion during a pause in the reads longer than a quarter
// of a second, a load, is dropped.
struct InputMouseLook {
    float yaw = 0.0f;
    float pitch = 0.0f;
    // Which read of pad 0 this is, counting from 1.
    std::uint64_t read = 0;
    // Wheel notches, positive zooms in. Only emitted with input.mouse_zoom enabled outside menus.
    float zoom = 0.0f;
    bool  zoom_reset = false;
};

const InputMouseLook &InputGetMouseLook();

// The game read pad 0's left stick (CGamePad::GetLX/GetLY, port/src/gamepad.cpp).
void InputNoteLeftStickRead();

// Whether the game read the left stick between the last two latches of pad 0.
bool InputLeftStickLive();

// The pad as the game reads it: the device state or the override, stick_dpad applied.
const InputPadState &InputGetPad(int pad);

void InputSetLookOnLeftStick(bool left);

void InputSetRumble(int pad, InputRumble rumble);

// Colours a DualSense's lightbar (and any pad with an RGB light) while input.lightbar is on. A pad
// without one, or no pad, ignores it.
void InputSetLightbar(int pad, std::uint8_t red, std::uint8_t green, std::uint8_t blue);

InputRumble InputGetRumble(int pad);

// Replaces what InputPoll reads for one pad, for tests and replays; nullptr
// hands the pad back to the devices.
void InputSetOverride(int pad, const InputPadState *state);

// base with the keyboard and mouse folded in through the bindings: buttons add, and an axis takes
// the keyboard and mouse deflection unless base deflects it past the game's dead zone.
InputPadState InputApplyKeyboardMouse(InputPadState base, const InputKeyboardMouse &held);

// The stick byte the game's AxisCalibration reads as deflection * 128, deflection in [-1, 1]: the
// dead zone is stepped over, so any motion moves the game.
std::uint8_t InputStickByte(float deflection);

// Rebinds one action ("cross", "up", "lx-", "rx", ...) to the names given: SDL key names, Mouse1 to
// Mouse5 (left, right, middle, X1, X2), and for the whole-axis actions lx ly rx ry MouseX or MouseY
// with an optional sign and scale (-MouseY, MouseX*0.5), which make the mouse a stick instead of
// the camera's own. Returns false if the action or a name is unknown or does not fit the action.
bool InputBindKeys(std::string_view action, std::span<const std::string_view> keys);

// Restores the default bindings and mouse settings.
void InputResetBindings();

void InputSetMouseSettings(const InputMouseSettings &settings);

const InputMouseSettings &InputGetMouseSettings();

// The mouse as a menu's pointer: its motion since the last take, in window pixels (y downward), the
// wheel's turn in notches (positive away from the user), and the buttons held (bit n-1 for Mouse n).
struct InputMenuMouse {
    float         dx = 0.0f;
    float         dy = 0.0f;
    float         wheel = 0.0f;
    std::uint32_t buttons = 0;
};

// While on, the mouse is a menu's pointer: its motion and buttons reach InputTakeMenuMouse and no
// longer press pad 0's bound buttons or turn its stick. Capture works as ever.
void InputSetMenuMouse(bool on);

void InputSetMenuNavigation(bool on);

void InputSetDeveloperMenu(bool on);
bool InputDeveloperMenu();

// Takes the pointer's motion and wheel since the last call, live and scripted.
InputMenuMouse InputTakeMenuMouse();

// The family of button symbols that matches the device last used: keyboard and mouse after a key,
// a mouse button or the mouse moved, otherwise the first gamepad's make (PS4 before one has been
// touched; Xbox for a pad of unknown make).
enum class InputGlyphFamily {
    Ps4,
    Ps5,
    Xbox,
    Switch,
    Keyboard,
    Ps3,
    SteamDeck,
    SteamController,
};

// The family for a gamepad from what SDL reports of it: its SDL_GamepadType (as an int), USB vendor
// and product. PlayStation, Xbox and Nintendo pads by type; Valve's pads (vendor 0x28DE) by product,
// the Steam Deck apart from the Steam Controllers (the 2015 one and the 2026 one); a pad SDL does not
// know is drawn as an Xbox pad, the layout most pads copy.
InputGlyphFamily InputGlyphFamilyForGamepad(int sdl_gamepad_type, unsigned vendor, unsigned product);

InputGlyphFamily InputActiveGlyphFamily();

// The first key or mouse button bound to a pad action ("cross", "start", "up"...), named as the
// glyph table spells them: SDL's scancode name lower-cased with spaces dropped ("space", "return",
// "leftshift"), or "mouse1" to "mouse5" (left, right, middle, X1, X2). Empty if none is bound.
std::string InputPrimaryBindingName(std::string_view action);

// SDL scancode for a key name, any case; `_` stands for a space ("left_shift"), and Grave, Backquote
// and Backtick name the key left of 1. -1 if unknown.
int InputScancodeFromName(std::string_view name);

// Keys that drive the port rather than a pad, bound like the pad's actions ("fps_toggle"), which
// also take Gamepad:<SDL gamepad button name> (Gamepad:guide).
enum class InputHostAction {
    FpsToggle,
    DeveloperMenu,
    DebugMenu,
    GyroHold,
    ZoomReset,
};

// Whether a key, mouse button or gamepad button bound to the action is held, live or scripted.
bool InputHostHeld(InputHostAction action);

// Consumes one press of the action since the last call that returned true. Every key-down (not a
// repeat) counts, so a press released before the next poll is not lost.
bool InputHostPressed(InputHostAction action);

// The keyboard and mouse the input script holds now; host actions read them beside the live devices,
// and a key that becomes held counts as a press.
void InputSetScriptedDevices(const InputKeyboardMouse &held);

// Movement bound to keyboard keys, independent of controller look and mouse axes.
// First-person gameplay uses it to walk while the controller retains retail look controls.
struct InputKeyboardMovement {
    float x = 0.0f;
    float y = 0.0f;
};

InputKeyboardMovement InputGetKeyboardMovement();

// Mirrors pad 0 gameplay key lock for the independent keyboard movement channel.
void InputSetMovementLocked(bool locked);

// One action of the binding table, for a bindings menu. host is true for the actions the port reads
// itself (fps_toggle, debug_menu, ...) rather than the game; whole_axis is true for lx/ly/rx/ry,
// which only take MouseX/MouseY and so cannot be bound to a key.
struct InputActionInfo {
    std::string_view action;
    bool             host = false;
    bool             whole_axis = false;
};

// Every action of the binding table, in the order the game reads them.
std::span<const InputActionInfo> InputActions();

// Whether the action also takes "Gamepad:<button>" sources (the host actions do).
bool InputActionTakesGamepad(std::string_view action);

// The keys, mouse buttons and gamepad buttons now bound to an action, as text ("F, Mouse1"); "None"
// when nothing is bound and "" when the action is unknown.
std::string InputBindingLabel(std::string_view action);

// Opens a rebind capture: the keys and buttons held now become the set already seen, so the press
// that opens the capture is not itself bound.
void InputBeginBindCapture();

// While a capture is open: the name of the key, mouse button or gamepad button that went down since
// the last call, or "" when none. The name reads back through InputBindKeys (SDL key names, Mouse1
// to Mouse5, Gamepad:<button>); "Escape" is reported so a caller can cancel. Gamepad buttons are
// only reported when allow_gamepad is set.
std::string InputTakeBindKey(bool allow_gamepad);

// Whether the source InputTakeBindKey named is held now. A caller that has just bound it keeps
// swallowing input until it is let go, so the press that made the binding does not also act on the
// action it was given to.
bool InputBindSourceHeld(std::string_view name);
