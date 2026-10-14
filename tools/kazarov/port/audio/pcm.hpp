#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace audio {

// A recording the soundtrack option plays in place of a sequence: interleaved 16-bit stereo, whatever
// the file held, at the file's own rate. Loop points are in frames; loop_end 0 is the file's end.
struct PcmTrack {
    std::vector<std::int16_t> samples;
    int                       rate = 48000;
    std::int64_t              start = 0;
    std::int64_t              loop_start = 0;
    std::int64_t              loop_end = 0;
    bool                      loop = true;
    float                     gain = 1.0f;

    std::int64_t Frames() const { return static_cast<std::int64_t>(samples.size() / 2); }
};

// Decodes a WAV, FLAC or Ogg Vorbis file (by its extension). Null, with the reason in error, when the
// file cannot be read or decoded.
std::shared_ptr<PcmTrack> LoadPcmTrack(const std::filesystem::path &path, std::string &error);

} // namespace audio
