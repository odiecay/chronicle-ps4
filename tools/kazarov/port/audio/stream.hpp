#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "pcm.hpp"

namespace audio {

enum class StreamState { Idle, Playing, Ended };

class StreamChannel {
public:
    void Open(std::shared_ptr<const PcmTrack> track);

    void Close();

    bool IsOpen() const;

    int Channels() const;

    void SetSourceChannels(int channels);

    void Play();

    void Stop();

    void Seek(std::int64_t frame);

    void SetLoop(bool loop);

    bool Loop() const;

    void SetVolume(float left, float right);

    StreamState State() const;

    std::int64_t Position() const;

    std::int64_t FramesSinceEnd() const;

    std::uint32_t Level() const;

    void Advance(std::int64_t frames, int rate);

    void Render(float *stereo, int frames, int rate, float gain);

private:
    void SetState(StreamState state);

    void CountSinceEnd(std::int64_t frames);

    void Run(float *stereo, std::int64_t frames, int rate, float gain);

    mutable std::mutex              mutex_;
    std::shared_ptr<const PcmTrack> track_;
    double                          position_ = 0.0;
    StreamState                     state_ = StreamState::Idle;
    std::int64_t                    frames_since_end_ = 0;
    int                             channels_ = 1;
    float                           volume_l_ = 1.0f;
    float                           volume_r_ = 1.0f;
    bool                            loop_ = false;
};

} // namespace audio
