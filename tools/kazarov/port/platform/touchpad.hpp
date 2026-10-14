#pragma once

#include <cstdint>

// A DualSense's touchpad as a mouse for the menus: one finger moves the pointer, pressing the pad
// is the left button, the Create button the right, two fingers scroll, and a quick two-finger tap
// is a right click. Pure; platform/input.cpp feeds it the pad's fingers each poll.

struct TouchpadFinger {
    bool  down = false;
    // 0 to 1 across the pad, y downward.
    float x = 0.0f;
    float y = 0.0f;
};

struct TouchpadFrame {
    TouchpadFinger finger[2];
    bool           click = false;  // the pad pressed in
    bool           create = false; // the Create button
};

struct TouchpadStep {
    // Pointer motion in window pixels, y downward.
    float dx = 0.0f;
    float dy = 0.0f;
    // Whole wheel notches, positive away from the user (scrolling up).
    float wheel = 0.0f;
    // Mouse buttons (bit n-1 for Mouse n) held now.
    std::uint32_t held = 0;
    // Mouse buttons tapped this step: down and up, to be shown as one click.
    std::uint32_t tapped = 0;
};

class TouchpadGestures {
public:
    // Scales pointer motion; 1 takes a swipe across the pad to a swipe across 1500 window pixels.
    void SetSensitivity(float sensitivity) { sensitivity_ = sensitivity; }

    TouchpadStep Update(const TouchpadFrame &frame, std::uint64_t now_ms);

    // Forgets the fingers, as when the pad changes hands.
    void Reset();

private:
    int            count_ = 0;
    TouchpadFinger last_[2];
    float          sensitivity_ = 1.0f;
    float          wheel_remainder_ = 0.0f;
    bool           tap_ = false;
    float          tap_travel_ = 0.0f;
    std::uint64_t  tap_start_ = 0;
};
