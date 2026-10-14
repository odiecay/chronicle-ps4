#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

#include "audio/hsmsg.hpp"

namespace {

using audio::HsMsg;
using audio::HsMsgKind;

struct Case {
    std::array<std::uint8_t, audio::kHsMsgBytes> bytes;
    HsMsg                                        msg;
};

const std::vector<Case> kGameMessages = {
    {{0xF9, 0x00, 0x00, 100, 0x00, 0x00, 0x00}, {HsMsgKind::PortVolume, 0, 0, 100}},
    {{0xF9, 0x01, 0x00, 32, 0x00, 0x00, 0x00}, {HsMsgKind::PortPan, 0, 0, 32}},
    {{0xFD, 0x10, 0x00, 5, 3, 127, 0x00}, {HsMsgKind::KeyOn, 5, 3, 127}},
    {{0xFD, 0x10, 0x00, 5, 3, 0x00, 0x00}, {HsMsgKind::KeyOff, 5, 3, 0}},
    {{0xFD, 0x00, 0x00, 5, 3, 90, 0x00}, {HsMsgKind::Volume, 5, 3, 90}},
    {{0xFD, 0x01, 0x00, 5, 3, 20, 0x00}, {HsMsgKind::Pan, 5, 3, 20}},
    {{0xFD, 0x02, 0x00, 5, 3, 0x7F, 0x7F}, {HsMsgKind::Pitch, 5, 3, 0x3FFF}},
    {{0xFD, 0x02, 0x00, 5, 3, 0x00, 0x40}, {HsMsgKind::Pitch, 5, 3, 0x2000}},
};

std::optional<HsMsg> Decode(std::initializer_list<std::uint8_t> bytes) {
    const std::vector<std::uint8_t> message(bytes);
    return audio::DecodeHsMsg(message);
}

void ExpectSame(const std::optional<HsMsg> &actual, const HsMsg &expected) {
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->kind, expected.kind);
    EXPECT_EQ(actual->key, expected.key);
    EXPECT_EQ(actual->id, expected.id);
    EXPECT_EQ(actual->value, expected.value);
}

} // namespace

TEST(AudioHsMsg, DecodesTheBytesTheGameSends) {
    for (const Case &c : kGameMessages) {
        SCOPED_TRACE(testing::PrintToString(c.msg.kind));
        ExpectSame(audio::DecodeHsMsg(c.bytes), c.msg);
    }
}

TEST(AudioHsMsg, EncodesTheGameMessagesToTheSameBytes) {
    for (const Case &c : kGameMessages) {
        SCOPED_TRACE(testing::PrintToString(c.msg.kind));
        EXPECT_EQ(audio::EncodeHsMsg(c.msg), c.bytes);
        const auto decoded = audio::DecodeHsMsg(c.bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(audio::EncodeHsMsg(*decoded), c.bytes);
    }
}

TEST(AudioHsMsg, RoundTripsEveryKindAcrossValues) {
    const std::vector<HsMsg> cases = {
        {HsMsgKind::PortVolume, 0, 0, 0},
        {HsMsgKind::PortVolume, 0, 0, 127},
        {HsMsgKind::PortPan, 0, 0, 1},
        {HsMsgKind::PortPan, 0, 0, 64},
        {HsMsgKind::KeyOn, 0, 0, 1},
        {HsMsgKind::KeyOn, 5, 3, 64},
        {HsMsgKind::KeyOn, 127, 0x7E, 127},
        {HsMsgKind::KeyOff, 0, 0, 0},
        {HsMsgKind::KeyOff, 5, 3, 0},
        {HsMsgKind::KeyOff, 127, 0x7E, 0},
        {HsMsgKind::Volume, 0, 0, 0},
        {HsMsgKind::Volume, 5, 3, 127},
        {HsMsgKind::Pan, 0, 0, 0},
        {HsMsgKind::Pan, 5, 3, 64},
        {HsMsgKind::Pan, 127, 0x7E, 127},
        {HsMsgKind::Pitch, 0, 0, 0},
        {HsMsgKind::Pitch, 5, 3, 1},
        {HsMsgKind::Pitch, 5, 3, 0x7F},
        {HsMsgKind::Pitch, 5, 3, 0x80},
        {HsMsgKind::Pitch, 5, 3, 0x2000},
        {HsMsgKind::Pitch, 127, 0x7E, 0x3FFF},
    };
    for (const HsMsg &m : cases) {
        SCOPED_TRACE(testing::PrintToString(m.kind));
        const auto bytes = audio::EncodeHsMsg(m);
        ExpectSame(audio::DecodeHsMsg(bytes), m);
    }
}

TEST(AudioHsMsg, RejectsShortOrUnknownMessages) {
    EXPECT_FALSE(Decode({}).has_value());
    EXPECT_FALSE(Decode({0xF9}).has_value());
    EXPECT_FALSE(Decode({0xF9, 0x00, 0x00}).has_value());
    EXPECT_FALSE(Decode({0xFD, 0x10, 0x00, 5, 3}).has_value());
    EXPECT_FALSE(Decode({0xFD, 0x02, 0x00, 5, 3, 1}).has_value());
    EXPECT_FALSE(Decode({0xF8, 0x00, 0x00, 0, 0, 0, 0}).has_value());
    EXPECT_FALSE(Decode({0xFD, 0x03, 0x00, 5, 3, 1, 0}).has_value());
    EXPECT_FALSE(Decode({0xF9, 0x02, 0x00, 1}).has_value());
}

TEST(AudioHsMsg, MasksDataBytesToSevenBits) {
    ExpectSame(Decode({0xFD, 0x10, 0x00, 0x85, 0x83, 0xFF, 0x00}), {HsMsgKind::KeyOn, 5, 3, 127});
    ExpectSame(Decode({0xFD, 0x02, 0x00, 0x85, 0x83, 0xFF, 0xFF}), {HsMsgKind::Pitch, 5, 3, 0x3FFF});
    ExpectSame(Decode({0xF9, 0x00, 0x00, 0xE4, 0x00, 0x00, 0x00}), {HsMsgKind::PortVolume, 0, 0, 100});
    EXPECT_EQ(audio::EncodeHsMsg({HsMsgKind::KeyOn, 0x85, 0x83, 0xFF}),
              (std::array<std::uint8_t, audio::kHsMsgBytes>{0xFD, 0x10, 0x00, 5, 3, 127, 0x00}));
}
