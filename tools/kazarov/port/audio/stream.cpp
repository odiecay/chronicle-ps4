#include "stream.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace audio {

namespace {

constexpr std::int64_t kMaxFramesSinceEnd = std::numeric_limits<std::int64_t>::max() / 2;

} // namespace

void StreamChannel::Open(std::shared_ptr<const PcmTrack> track) {
    std::lock_guard lock(mutex_);
    track_ = std::move(track);
    position_ = 0.0;
    SetState(StreamState::Idle);
}

void StreamChannel::Close() {
    std::lock_guard lock(mutex_);
    track_.reset();
    position_ = 0.0;
    SetState(StreamState::Idle);
}

bool StreamChannel::IsOpen() const {
    std::lock_guard lock(mutex_);
    return track_ != nullptr;
}

int StreamChannel::Channels() const {
    std::lock_guard lock(mutex_);
    return channels_;
}

void StreamChannel::SetSourceChannels(int channels) {
    std::lock_guard lock(mutex_);
    channels_ = std::clamp(channels, 1, 2);
}

void StreamChannel::Play() {
    std::lock_guard lock(mutex_);
    if (track_ == nullptr || state_ == StreamState::Playing) {
        return;
    }
    if (state_ == StreamState::Ended) {
        position_ = 0.0;
    }
    SetState(StreamState::Playing);
}

void StreamChannel::Stop() {
    std::lock_guard lock(mutex_);
    SetState(StreamState::Idle);
}

void StreamChannel::Seek(std::int64_t frame) {
    std::lock_guard lock(mutex_);
    const std::int64_t length = track_ != nullptr ? track_->Frames() : 0;
    position_ = static_cast<double>(std::clamp<std::int64_t>(frame, 0, length));
    if (state_ == StreamState::Ended) {
        SetState(StreamState::Idle);
    }
}

void StreamChannel::SetLoop(bool loop) {
    std::lock_guard lock(mutex_);
    loop_ = loop;
}

bool StreamChannel::Loop() const {
    std::lock_guard lock(mutex_);
    return loop_;
}

void StreamChannel::SetVolume(float left, float right) {
    std::lock_guard lock(mutex_);
    volume_l_ = left;
    volume_r_ = right;
}

StreamState StreamChannel::State() const {
    std::lock_guard lock(mutex_);
    return state_;
}

std::int64_t StreamChannel::Position() const {
    std::lock_guard lock(mutex_);
    return static_cast<std::int64_t>(position_);
}

std::int64_t StreamChannel::FramesSinceEnd() const {
    std::lock_guard lock(mutex_);
    return frames_since_end_;
}

std::uint32_t StreamChannel::Level() const {
    std::lock_guard lock(mutex_);
    if (state_ != StreamState::Playing) {
        return 0;
    }
    const std::int64_t length = track_->Frames();
    if (length == 0) {
        return 0;
    }
    const std::int64_t frame = std::min(static_cast<std::int64_t>(position_), length - 1);
    const auto         left = static_cast<std::uint16_t>(track_->samples[static_cast<std::size_t>(frame) * 2]);
    const auto         right = static_cast<std::uint16_t>(track_->samples[static_cast<std::size_t>(frame) * 2 + 1]);
    return left | (static_cast<std::uint32_t>(right) << 16);
}

void StreamChannel::Advance(std::int64_t frames, int rate) {
    std::lock_guard lock(mutex_);
    Run(nullptr, frames, rate, 0.0f);
}

void StreamChannel::Render(float *stereo, int frames, int rate, float gain) {
    std::lock_guard lock(mutex_);
    Run(stereo, frames, rate, gain);
}

void StreamChannel::SetState(StreamState state) {
    state_ = state;
    frames_since_end_ = 0;
}

void StreamChannel::CountSinceEnd(std::int64_t frames) {
    frames_since_end_ += std::min(frames, kMaxFramesSinceEnd - frames_since_end_);
}

void StreamChannel::Run(float *stereo, std::int64_t frames, int rate, float gain) {
    if (frames <= 0) {
        return;
    }
    if (state_ == StreamState::Ended) {
        CountSinceEnd(frames);
        return;
    }
    if (state_ != StreamState::Playing || rate <= 0) {
        return;
    }

    const PcmTrack &track = *track_;
    const std::int64_t length = track.Frames();
    const double       step = track.rate / static_cast<double>(rate);
    const float        scale_l = volume_l_ * gain / 32768.0f;
    const float        scale_r = volume_r_ * gain / 32768.0f;
    for (std::int64_t i = 0; i < frames; ++i) {
        if (position_ >= static_cast<double>(length)) {
            if (!loop_ || length == 0) {
                position_ = static_cast<double>(length);
                SetState(StreamState::Ended);
                // Frames after the end within this call already fall past it.
                CountSinceEnd(frames - i);
                return;
            }
            position_ = std::fmod(position_, static_cast<double>(length));
        }
        if (stereo != nullptr) {
            const std::int64_t  at = static_cast<std::int64_t>(position_);
            const float         fraction = static_cast<float>(position_ - at);
            const std::int64_t  next = std::min(at + 1, length - 1);
            const std::int16_t *a = &track.samples[static_cast<std::size_t>(at) * 2];
            const std::int16_t *b = &track.samples[static_cast<std::size_t>(next) * 2];
            stereo[i * 2] += (a[0] + (b[0] - a[0]) * fraction) * scale_l;
            stereo[i * 2 + 1] += (a[1] + (b[1] - a[1]) * fraction) * scale_r;
        }
        position_ += step;
    }
}

} // namespace audio
