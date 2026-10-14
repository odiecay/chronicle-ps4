#include "hsmsg.hpp"

namespace audio {

namespace {

constexpr std::uint8_t kPortStatus = 0xF9;
constexpr std::uint8_t kEffectStatus = 0xFD;
constexpr std::uint8_t kVolumeSub = 0x00;
constexpr std::uint8_t kPanSub = 0x01;
constexpr std::uint8_t kKeySub = 0x10;
constexpr std::uint8_t kPitchSub = 0x02;
constexpr int          kDataMask = 0x7F;
constexpr std::size_t  kPortBytes = 4;
constexpr std::size_t  kEffectBytes = 6;
constexpr std::size_t  kPitchBytes = 7;

int Data(std::uint8_t byte) {
    return byte & kDataMask;
}

} // namespace

std::optional<HsMsg> DecodeHsMsg(std::span<const std::uint8_t> message) {
    if (message.size() < 2) {
        return std::nullopt;
    }
    const std::uint8_t status = message[0];
    const std::uint8_t sub = message[1];

    if (status == kPortStatus) {
        if (message.size() < kPortBytes) {
            return std::nullopt;
        }
        const int value = Data(message[3]);
        if (sub == kVolumeSub) {
            return HsMsg{HsMsgKind::PortVolume, 0, 0, value};
        }
        if (sub == kPanSub) {
            return HsMsg{HsMsgKind::PortPan, 0, 0, value};
        }
        return std::nullopt;
    }

    if (status != kEffectStatus || message.size() < (sub == kPitchSub ? kPitchBytes : kEffectBytes)) {
        return std::nullopt;
    }
    const int key = Data(message[3]);
    const int id = Data(message[4]);
    const int value = Data(message[5]);
    switch (sub) {
        case kKeySub:
            if (value == 0) {
                return HsMsg{HsMsgKind::KeyOff, key, id, 0};
            }
            return HsMsg{HsMsgKind::KeyOn, key, id, value};
        case kVolumeSub:
            return HsMsg{HsMsgKind::Volume, key, id, value};
        case kPanSub:
            return HsMsg{HsMsgKind::Pan, key, id, value};
        case kPitchSub:
            return HsMsg{HsMsgKind::Pitch, key, id, value | (Data(message[6]) << 7)};
        default:
            return std::nullopt;
    }
}

std::array<std::uint8_t, kHsMsgBytes> EncodeHsMsg(const HsMsg &message) {
    std::array<std::uint8_t, kHsMsgBytes> bytes{};
    const auto                            data = [](int value) { return static_cast<std::uint8_t>(value & kDataMask); };
    const auto                            effect = [&](std::uint8_t sub, int value) {
        bytes[0] = kEffectStatus;
        bytes[1] = sub;
        bytes[3] = data(message.key);
        bytes[4] = data(message.id);
        bytes[5] = data(value);
    };

    switch (message.kind) {
        case HsMsgKind::PortVolume:
            bytes[0] = kPortStatus;
            bytes[1] = kVolumeSub;
            bytes[3] = data(message.value);
            break;
        case HsMsgKind::PortPan:
            bytes[0] = kPortStatus;
            bytes[1] = kPanSub;
            bytes[3] = data(message.value);
            break;
        case HsMsgKind::KeyOn:
            effect(kKeySub, message.value);
            break;
        case HsMsgKind::KeyOff:
            effect(kKeySub, 0);
            break;
        case HsMsgKind::Volume:
            effect(kVolumeSub, message.value);
            break;
        case HsMsgKind::Pan:
            effect(kPanSub, message.value);
            break;
        case HsMsgKind::Pitch:
            effect(kPitchSub, message.value);
            bytes[6] = data(message.value >> 7);
            break;
    }
    return bytes;
}

} // namespace audio
