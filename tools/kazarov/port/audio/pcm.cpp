#include "pcm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

namespace audio {

namespace {

// Interleaved samples of any channel count to stereo: mono doubled, more than two cut to the front pair.
void ToStereo(const std::int16_t *in, std::uint64_t frames, int channels, std::vector<std::int16_t> &out) {
    out.resize(static_cast<std::size_t>(frames) * 2);
    for (std::uint64_t i = 0; i < frames; ++i) {
        const std::int16_t left = in[i * channels];
        out[i * 2] = left;
        out[i * 2 + 1] = channels > 1 ? in[i * channels + 1] : left;
    }
}

} // namespace

std::shared_ptr<PcmTrack> LoadPcmTrack(const std::filesystem::path &path, std::string &error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open the file";
        return nullptr;
    }
    const std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string             extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });

    auto     track = std::make_shared<PcmTrack>();
    unsigned channels = 0;
    unsigned rate = 0;
    bool     decoded = false;
    if (extension == ".wav") {
        drwav_uint64  frames = 0;
        std::int16_t *pcm = drwav_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &channels, &rate, &frames, nullptr);
        if (pcm != nullptr) {
            ToStereo(pcm, frames, static_cast<int>(channels), track->samples);
            drwav_free(pcm, nullptr);
            decoded = true;
        }
    } else if (extension == ".flac") {
        drflac_uint64 frames = 0;
        std::int16_t *pcm = drflac_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &channels, &rate, &frames, nullptr);
        if (pcm != nullptr) {
            ToStereo(pcm, frames, static_cast<int>(channels), track->samples);
            drflac_free(pcm, nullptr);
            decoded = true;
        }
    } else if (extension == ".ogg") {
        int       ogg_channels = 0;
        int       ogg_rate = 0;
        short    *ogg = nullptr;
        const int count = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char *>(data.data()),
                                                   static_cast<int>(data.size()), &ogg_channels, &ogg_rate, &ogg);
        if (count > 0 && ogg != nullptr) {
            rate = static_cast<unsigned>(ogg_rate);
            ToStereo(ogg, static_cast<std::uint64_t>(count), ogg_channels, track->samples);
            decoded = true;
        }
        std::free(ogg);
    } else {
        error = "unsupported type (use .wav, .flac or .ogg)";
        return nullptr;
    }
    if (!decoded || track->samples.empty() || rate == 0) {
        error = "cannot decode the file";
        return nullptr;
    }
    track->rate = static_cast<int>(rate);
    return track;
}

} // namespace audio
