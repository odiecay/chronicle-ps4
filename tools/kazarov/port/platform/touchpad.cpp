#include "platform/touchpad.hpp"

#include <cmath>

namespace {

// Window pixels a swipe across the pad's width and height moves the pointer; the pad is about twice
// as wide as tall.
constexpr float kPixelsAcross = 1500.0f;
constexpr float kPixelsDown = 750.0f;

// Notches a two-finger swipe the pad's height scrolls.
constexpr float kNotchesPerPad = 8.0f;

// A two-finger tap is quick and barely moves.
constexpr std::uint64_t kTapMilliseconds = 300;
constexpr float         kTapTravel = 0.06f;

constexpr std::uint32_t kLeft = 1u << 0;
constexpr std::uint32_t kRight = 1u << 1;

} // namespace

void TouchpadGestures::Reset() {
    count_ = 0;
    last_[0] = last_[1] = TouchpadFinger{};
    wheel_remainder_ = 0.0f;
    tap_ = false;
}

TouchpadStep TouchpadGestures::Update(const TouchpadFrame &frame, std::uint64_t now_ms) {
    TouchpadStep step;
    step.held = (frame.click ? kLeft : 0u) | (frame.create ? kRight : 0u);

    int count = (frame.finger[0].down ? 1 : 0) + (frame.finger[1].down ? 1 : 0);
    // Which finger is down by count alone is enough: a pair is both, a single is whichever is set.
    const TouchpadFinger *single = frame.finger[0].down ? &frame.finger[0] : &frame.finger[1];

    if (count == 1 && count_ == 1) {
        const TouchpadFinger &before = last_[0];
        step.dx = (single->x - before.x) * kPixelsAcross * sensitivity_;
        step.dy = (single->y - before.y) * kPixelsDown * sensitivity_;
    } else if (count == 2 && count_ == 2) {
        float moved_y = ((frame.finger[0].y - last_[0].y) + (frame.finger[1].y - last_[1].y)) * 0.5f;
        wheel_remainder_ += moved_y * kNotchesPerPad;
        float notches = std::trunc(wheel_remainder_);
        wheel_remainder_ -= notches;
        step.wheel = notches;
        tap_travel_ += std::fabs(frame.finger[0].x - last_[0].x) + std::fabs(frame.finger[0].y - last_[0].y) +
                       std::fabs(frame.finger[1].x - last_[1].x) + std::fabs(frame.finger[1].y - last_[1].y);
    }

    if (count == 2 && count_ < 2 && !tap_) {
        // Both fingers came down: a candidate tap.
        tap_ = true;
        tap_travel_ = 0.0f;
        tap_start_ = now_ms;
        wheel_remainder_ = 0.0f;
    }
    if (tap_ && (tap_travel_ > kTapTravel || now_ms - tap_start_ > kTapMilliseconds)) {
        tap_ = false;
    }
    if (count == 0) {
        if (tap_ && now_ms - tap_start_ <= kTapMilliseconds) {
            step.tapped |= kRight;
        }
        tap_ = false;
        wheel_remainder_ = 0.0f;
    }

    count_ = count;
    last_[0] = frame.finger[0];
    last_[1] = frame.finger[1];
    // A single finger is tracked in slot 0 whichever SDL finger it is.
    if (count == 1) {
        last_[0] = *single;
    }
    return step;
}
