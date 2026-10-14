#include "audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "audio/wav.hpp"
#include "clock.hpp"
#include "settings.hpp"

namespace {

constexpr int kChunkFrames = 1024;

SDL_AudioStream   *g_stream = nullptr;
AudioRenderFn      g_render = nullptr;
void              *g_user = nullptr;
std::vector<float> g_chunk;
int                g_rate = 48000;
bool               g_surround = false;

// Stereo to 5.1 in SDL's channel order: front left and right, centre, bass, back left and right.
// The rears carry the side signal (left minus right) 12 ms late and with the top rolled off, the
// way a matrix decoder places ambience behind the listener; the centre is the sum.
struct Upmix {
    static constexpr int kRearChannels = 6;
    std::vector<float>   delay;
    std::size_t          at = 0;
    float                bass = 0.0f;
    float                rear = 0.0f;

    void Reset(int rate) {
        delay.assign(std::max<std::size_t>(1, static_cast<std::size_t>(rate * 12 / 1000)), 0.0f);
        at = 0;
        bass = rear = 0.0f;
    }

    void Run(const float *in, float *out, int frames, int rate) {
        const float bass_k = 1.0f - std::exp(-6.2831853f * 120.0f / static_cast<float>(rate));
        const float rear_k = 1.0f - std::exp(-6.2831853f * 7000.0f / static_cast<float>(rate));
        for (int i = 0; i < frames; i++) {
            const float l = in[i * 2];
            const float r = in[i * 2 + 1];
            const float mid = (l + r) * 0.5f;
            const float side = (l - r) * 0.5f;
            const float late = delay[at];
            delay[at] = side;
            at = (at + 1) % delay.size();
            bass += (mid - bass) * bass_k;
            rear += (late - rear) * rear_k;
            float *o = out + i * kRearChannels;
            o[0] = l * 0.85f;
            o[1] = r * 0.85f;
            o[2] = mid * 0.7f;
            o[3] = bass;
            o[4] = rear * 0.9f;
            o[5] = -rear * 0.9f;
        }
    }
};

Upmix              g_upmix;
std::vector<float> g_wide;

void SDLCALL Feed(void *, SDL_AudioStream *stream, int additional_amount, int) {
    const int channels = g_surround ? 6 : 2;
    int       frames = additional_amount / static_cast<int>(sizeof(float) * channels);
    while (frames > 0) {
        const int count = std::min(frames, kChunkFrames);
        g_render(g_user, g_chunk.data(), count);
        if (g_surround) {
            g_wide.resize(static_cast<std::size_t>(count) * 6);
            g_upmix.Run(g_chunk.data(), g_wide.data(), count, g_rate);
            SDL_PutAudioStreamData(stream, g_wide.data(), count * static_cast<int>(sizeof(float) * 6));
        } else {
            SDL_PutAudioStreamData(stream, g_chunk.data(), count * static_cast<int>(sizeof(float) * 2));
        }
        frames -= count;
    }
}

// <prefix>_AUDIO_WAV=<path>: the mixer is pulled on the game thread as the clock ticks, so N ticks write
// N / tick rate seconds whatever the run's real speed; a headless run records exactly what it
// would have played.
struct WavCapture {
    audio::WavWriter writer;
    int              rate = 0;
    std::int64_t     last_tick = 0;
    double           owed = 0.0;
};

WavCapture g_wav;

void CaptureTicks() {
    const std::int64_t tick = ClockTickCount();
    if (tick > g_wav.last_tick) {
        g_wav.owed += static_cast<double>(tick - g_wav.last_tick) * g_wav.rate / ClockTickRate();
    }
    g_wav.last_tick = tick;
    auto frames = static_cast<int>(g_wav.owed);
    g_wav.owed -= frames;
    while (frames > 0) {
        const int count = std::min(frames, kChunkFrames);
        g_render(g_user, g_chunk.data(), count);
        g_wav.writer.Append(g_chunk.data(), count);
        frames -= count;
    }
}

bool StartCapture(const char *path, int rate) {
    if (!g_wav.writer.Open(path, rate)) {
        std::fprintf(stderr, "audio: cannot write %s\n", path);
        return false;
    }
    g_wav.rate = rate;
    g_wav.last_tick = ClockTickCount();
    g_wav.owed = 0.0;
    ClockAddPumpHook(CaptureTicks);
    return true;
}

} // namespace

bool AudioOutputStart(int rate, AudioRenderFn render, void *user) {
    if (AudioOutputRunning()) {
        return true;
    }
    g_render = render;
    g_user = user;
    g_rate = rate;
    g_upmix.Reset(rate);
    g_chunk.assign(kChunkFrames * 2, 0.0f);
    if (const char *path = SettingsEnv("AUDIO_WAV"); path != nullptr && *path != '\0') {
        return StartCapture(path, rate);
    }
    if (const char *setting = SettingsEnv("AUDIO"); setting != nullptr && std::strcmp(setting, "off") == 0) {
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "audio: no audio subsystem: %s\n", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, g_surround ? 6 : 2, rate};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, nullptr);
    if (g_stream == nullptr) {
        std::fprintf(stderr, "audio: no playback device: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_stream);
    return true;
}

void AudioSetSurround(bool on) {
    if (on == g_surround) {
        return;
    }
    if (g_stream != nullptr) {
        // What the callback writes changes with the stream's source format, so the order matters:
        // the format first, then the flag the callback reads.
        const SDL_AudioSpec spec{SDL_AUDIO_F32, on ? 6 : 2, g_rate};
        SDL_LockAudioStream(g_stream);
        SDL_SetAudioStreamFormat(g_stream, &spec, nullptr);
        SDL_ClearAudioStream(g_stream);
        g_surround = on;
        g_upmix.Reset(g_rate);
        SDL_UnlockAudioStream(g_stream);
        return;
    }
    g_surround = on;
}

void AudioOutputStop() {
    if (g_wav.writer.IsOpen()) {
        ClockRemovePumpHook(CaptureTicks);
        g_wav.writer.Close();
    }
    if (g_stream == nullptr) {
        return;
    }
    SDL_DestroyAudioStream(g_stream);
    g_stream = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool AudioOutputRunning() {
    return g_stream != nullptr || g_wav.writer.IsOpen();
}
