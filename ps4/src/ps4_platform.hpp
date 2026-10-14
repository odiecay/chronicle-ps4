#pragma once

// PS4 (OpenOrbis) platform layer for Chronicle: Piglet (OpenGL ES 2 + EGL), the DualShock 4 and the
// user service. Adapted from alechurri's Ship of Harkinian PS4 port (MIT, see LICENSE-ps4-port).
//
// There is no SDL on the console. window_ps4.cpp, audio_ps4.cpp and host_ps4.cpp implement the
// platform headers over this file; sdl_ps4.cpp answers the few SDL calls the shared input code makes.

#include <cstdint>

namespace ps4 {

constexpr int kDisplayWidth = 1920;
constexpr int kDisplayHeight = 1080;

// Log lines go to stdout, which main_ps4.cpp points at a file.
void Log(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Loads the system modules and Piglet and creates the EGL window surface and ES 2 context. False
// (after logging why) when anything fails.
bool InitGraphics(int width, int height);
void SwapBuffers();
// Makes the EGL context current on the calling thread, or releases it from the thread (on = false).
bool MakeCurrent(bool on);
void ShutdownGraphics();
void *GetProc(const char *name);
// Takes down the system's boot splash; call once the first frame is about to be drawn.
void HideSplashScreen();

// The user service is shared by the pad and the audio output.
int32_t GetInitialUserId();

// One poll of the DualShock 4 of the user who started the game.
struct PadState {
    bool    connected = false;
    uint32_t buttons = 0;      // ORBIS_PAD_BUTTON_* bits
    uint8_t left_x = 128, left_y = 128, right_x = 128, right_y = 128;
    uint8_t l2 = 0, r2 = 0;
    // The touch pad's two fingers, 0..1919 by 0..941, and whether each is down.
    bool     touch_down[2] = {false, false};
    uint16_t touch_x[2] = {0, 0};
    uint16_t touch_y[2] = {0, 0};
    // Orientation sensors: acceleration in m/s^2 and angular velocity in rad/s.
    float accel[3] = {0, 0, 0};
    float gyro[3] = {0, 0, 0};
};

bool ReadPad(PadState &state);
void SetPadVibration(uint8_t large, uint8_t small);
void SetPadLight(uint8_t r, uint8_t g, uint8_t b);

// Writes one line with the state of the heap arena and of the system memory pools to the log.
void LogMemoryStats(const char *when);

} // namespace ps4
