#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "pcm.hpp"

namespace audio {

// Creates an empty version 1 mapping beside the save directory if none exists.
bool SoundtrackEnsure();

// soundtrack/soundtrack.json beside save/ and data/ names recordings to play instead of sequences:
//   {"version": 1, "overrides": {"e01a_a.sq": {"file": "norune.ogg", "gain": 1.0,
//                                            "loop": true, "loop_start": 12.5, "loop_end": 150.0}}}
// Keys are the original sequence filenames (<prefix>_AUDIO_TRACE=1 prints them). Times are seconds.
// Files are decoded when their song first starts.
//
// The recording for a sequence while the soundtrack option is on, or null: the option is off, the
// sequence has no entry, or its file will not decode (reported once on stderr).
std::shared_ptr<const PcmTrack> SoundtrackFor(std::string_view sequence);

// Reads the mapping again at the next lookup.
void SoundtrackReload();

} // namespace audio
