// platform/audio.hpp for the PS4: a feeder thread pulls the game's stereo mix and hands it to
// sceAudioOut at its fixed 48 kHz.

#ifdef __PS4__

#include "platform/audio.hpp"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <orbis/AudioOut.h>
#include <orbis/libkernel.h>

#include "ps4_platform.hpp"

namespace {

constexpr int kGrain = 256;   // frames per sceAudioOut call
constexpr int kOutRate = 48000;
// sceAudioOut takes the system user for a plain game port (the pad's user id is refused by Mbus).
constexpr int kSystemUserId = 0xFF;

AudioRenderFn g_render = nullptr;
void         *g_user = nullptr;
int           g_rate = kOutRate;
int           g_port = -1;
pthread_t     g_thread;
volatile bool g_running = false;
bool          g_started = false;

void *Feeder(void *) {
    std::vector<float>   out(static_cast<size_t>(kGrain) * 2);
    std::vector<int16_t> pcm(static_cast<size_t>(kGrain) * 2);
    // Source frames (stereo) not yet consumed, and where in them the next output frame falls.
    std::vector<float> pending;
    std::vector<float> chunk(static_cast<size_t>(kGrain) * 2);
    double             position = 0.0;
    const double       step = static_cast<double>(g_rate) / kOutRate;
    while (g_running) {
        if (g_rate == kOutRate) {
            g_render(g_user, out.data(), kGrain);
        } else {
            // Linear resampling from the mixer's rate.
            const double last_needed = position + step * (kGrain - 1) + 1.0;
            while (static_cast<double>(pending.size() / 2) <= last_needed + 1.0) {
                g_render(g_user, chunk.data(), kGrain);
                pending.insert(pending.end(), chunk.begin(), chunk.end());
            }
            for (int i = 0; i < kGrain; i++) {
                const size_t index = static_cast<size_t>(position);
                const float  frac = static_cast<float>(position - static_cast<double>(index));
                const float *a = &pending[index * 2];
                const float *b = a + 2;
                out[static_cast<size_t>(i) * 2] = a[0] + (b[0] - a[0]) * frac;
                out[static_cast<size_t>(i) * 2 + 1] = a[1] + (b[1] - a[1]) * frac;
                position += step;
            }
            const size_t consumed = static_cast<size_t>(position);
            pending.erase(pending.begin(), pending.begin() + static_cast<long>(consumed * 2));
            position -= static_cast<double>(consumed);
        }
        for (size_t i = 0; i < out.size(); i++) {
            float v = out[i] * 32767.0f;
            pcm[i] = static_cast<int16_t>(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
        }
        sceAudioOutOutput(g_port, pcm.data());
    }
    return nullptr;
}

} // namespace

bool AudioOutputStart(int rate, AudioRenderFn render, void *user) {
    if (g_started) {
        return true;
    }
    const char *setting = getenv("DC_AUDIO");
    if (setting != nullptr && strcmp(setting, "off") == 0) {
        return false;
    }
    g_render = render;
    g_user = user;
    g_rate = rate;
    sceAudioOutInit();
    g_port = sceAudioOutOpen(kSystemUserId, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kGrain, kOutRate,
                             ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (g_port < 0) {
        ps4::Log("[PS4] sceAudioOutOpen failed: 0x%08X", static_cast<unsigned>(g_port));
        return false;
    }
    g_running = true;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 256 * 1024);
    if (pthread_create(&g_thread, &attributes, Feeder, nullptr) != 0) {
        g_running = false;
        pthread_attr_destroy(&attributes);
        sceAudioOutClose(g_port);
        g_port = -1;
        return false;
    }
    pthread_attr_destroy(&attributes);
    g_started = true;
    ps4::Log("[PS4] audio running (%d Hz mix, %d Hz out)", rate, kOutRate);
    return true;
}

void AudioOutputStop() {
    if (!g_started) {
        return;
    }
    g_running = false;
    pthread_join(g_thread, nullptr);
    sceAudioOutClose(g_port);
    g_port = -1;
    g_started = false;
}

// The console's output is stereo; the 5.1 upmix is not offered.
void AudioSetSurround(bool) {}

bool AudioOutputRunning() { return g_started; }

#endif // __PS4__
