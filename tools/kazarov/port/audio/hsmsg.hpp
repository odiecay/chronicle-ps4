#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace audio {

inline constexpr std::size_t kHsMsgBytes = 7;

enum class HsMsgKind { PortVolume, PortPan, KeyOn, KeyOff, Volume, Pan, Pitch };

// Port kinds leave key and id at 0. value is the volume, pan, KeyOn velocity, 0 for KeyOff, or the
// 14-bit pitch, by kind.
struct HsMsg {
    HsMsgKind kind;
    int       key = 0;
    int       id = 0;
    int       value = 0;
};

std::optional<HsMsg> DecodeHsMsg(std::span<const std::uint8_t> message);

std::array<std::uint8_t, kHsMsgBytes> EncodeHsMsg(const HsMsg &message);

} // namespace audio
