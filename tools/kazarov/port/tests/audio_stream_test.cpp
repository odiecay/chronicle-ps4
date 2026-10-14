#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/hdbank.hpp"
#include "audio/hsmsg.hpp"
#include "audio/mixer.hpp"
#include "audio/pcm.hpp"
#include "audio/sq.hpp"
#include "audio/stream.hpp"
#include "audio_bank_fixture.hpp"

namespace fx = dc::test;

namespace {

constexpr int kPort = 7;
constexpr int kFrames = 4800;

std::shared_ptr<audio::PcmTrack> Stereo(const std::vector<std::int16_t> &left, const std::vector<std::int16_t> &right,
                                        int rate) {
    auto track = std::make_shared<audio::PcmTrack>();
    track->rate = rate;
    track->samples.resize(left.size() * 2);
    for (std::size_t i = 0; i < left.size(); i++) {
        track->samples[i * 2] = left[i];
        track->samples[i * 2 + 1] = right[i];
    }
    return track;
}

std::shared_ptr<audio::PcmTrack> Constant(std::int16_t value, std::size_t frames, int rate = 48000) {
    const std::vector<std::int16_t> samples(frames, value);
    return Stereo(samples, samples, rate);
}

std::vector<float> RenderChannel(audio::StreamChannel &channel, int frames, int rate = 48000) {
    std::vector<float> out(static_cast<std::size_t>(frames) * 2, 0.0f);
    channel.Render(out.data(), frames, rate, 1.0f);
    return out;
}

std::uint32_t Pack(std::int16_t left, std::int16_t right) {
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(left)) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(right)) << 16);
}

bool AllZero(const std::vector<float> &samples) {
    return std::all_of(samples.begin(), samples.end(), [](float s) { return s == 0.0f; });
}

std::shared_ptr<audio::Bank> MakeBank(const fx::BankSpec &spec) {
    const auto hd = fx::MakeHd(spec);
    const auto bd = fx::SquareVag(2);
    return audio::Bank::Create(hd, bd);
}

void Prepare(audio::Mixer &mixer, const std::shared_ptr<audio::Bank> &bank) {
    mixer.BindBank(kPort, bank);
    mixer.SetVolume(kPort, audio::kFullPortVolume);
    mixer.ShortMessage(kPort, 0xC0);
}

void KeyOn(audio::Mixer &mixer, std::uint8_t key) {
    const std::array<std::uint8_t, audio::kHsMsgBytes> message{0xFD, 0x10, 0x00, key, 1, 127, 0x00};
    mixer.HsMessage(kPort, message);
}

int Crossings(audio::Mixer &mixer) {
    return fx::ZeroCrossings(fx::RenderFrames(mixer, kFrames));
}

} // namespace

TEST(AudioStream, PlaysAtItsVolumeAndStopsAtTheEnd) {
    audio::StreamChannel channel;
    channel.Open(Constant(16384, 480));
    channel.SetVolume(0.5f, 0.25f);
    audio::Mixer mixer(48000);
    mixer.AddStreamChannel(&channel);
    channel.Play();

    const auto first = fx::RenderFrames(mixer, 240);
    for (std::size_t i = 0; i < first.size(); i += 2) {
        EXPECT_NEAR(first[i], 0.25f, 1e-3f);
        EXPECT_NEAR(first[i + 1], 0.125f, 1e-3f);
    }
    EXPECT_EQ(channel.State(), audio::StreamState::Playing);

    const auto second = fx::RenderFrames(mixer, 480);
    EXPECT_EQ(channel.State(), audio::StreamState::Ended);
    EXPECT_EQ(channel.FramesSinceEnd(), 240);
    EXPECT_NEAR(second[0], 0.25f, 1e-3f);
    const std::vector<float> tail(second.begin() + 480, second.end());
    EXPECT_TRUE(AllZero(tail));
}

TEST(AudioStream, StopKeepsThePositionAndPlayResumes) {
    audio::StreamChannel channel;
    channel.Open(Constant(16384, 480));
    channel.Play();
    RenderChannel(channel, 100);
    EXPECT_EQ(channel.Position(), 100);

    channel.Stop();
    EXPECT_EQ(channel.State(), audio::StreamState::Idle);
    EXPECT_TRUE(AllZero(RenderChannel(channel, 50)));
    EXPECT_EQ(channel.Position(), 100);

    channel.Play();
    const auto resumed = RenderChannel(channel, 10);
    EXPECT_EQ(channel.Position(), 110);
    EXPECT_NEAR(resumed[0], 0.5f, 1e-3f);
}

TEST(AudioStream, LoopsFromTheStartOfTheData) {
    std::vector<std::int16_t> ramp(100);
    for (std::size_t i = 0; i < ramp.size(); i++) {
        ramp[i] = static_cast<std::int16_t>(1000 + 10 * i);
    }
    audio::StreamChannel channel;
    channel.Open(Stereo(ramp, ramp, 48000));
    channel.SetLoop(true);
    channel.Play();

    const auto out = RenderChannel(channel, 150);
    EXPECT_EQ(channel.State(), audio::StreamState::Playing);
    EXPECT_FLOAT_EQ(out[200], out[0]);
    EXPECT_FLOAT_EQ(out[202], out[2]);
}

TEST(AudioStream, LevelIsTheRawSampleAtThePosition) {
    audio::StreamChannel channel;
    channel.Open(Stereo({-1000, -3000}, {2000, 4000}, 48000));
    EXPECT_EQ(channel.Level(), 0u);

    channel.Play();
    EXPECT_EQ(channel.Level(), Pack(-1000, 2000));
    RenderChannel(channel, 1);
    EXPECT_EQ(channel.Level(), Pack(-3000, 4000));

    channel.Stop();
    EXPECT_EQ(channel.Level(), 0u);
}

TEST(AudioStream, AdvanceEndsAStreamWithoutRendering) {
    audio::StreamChannel channel;
    channel.Open(Constant(16384, 100));
    channel.Play();

    channel.Advance(100, 48000);
    EXPECT_EQ(channel.State(), audio::StreamState::Playing);
    EXPECT_EQ(channel.Position(), 100);

    channel.Advance(30, 48000);
    EXPECT_EQ(channel.State(), audio::StreamState::Ended);
    EXPECT_EQ(channel.FramesSinceEnd(), 30);

    channel.Advance(10, 48000);
    EXPECT_EQ(channel.FramesSinceEnd(), 40);
}

TEST(AudioStream, ResamplesToTheMixerRate) {
    constexpr std::size_t kSourceFrames = 100;
    audio::StreamChannel channel;
    channel.Open(Constant(16384, kSourceFrames, 24000));
    channel.Play();

    const auto out = RenderChannel(channel, 2 * static_cast<int>(kSourceFrames), 48000);
    EXPECT_EQ(channel.State(), audio::StreamState::Playing);
    EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](float s) { return std::fabs(s - 0.5f) < 1e-3f; }));

    const auto after = RenderChannel(channel, 1, 48000);
    EXPECT_EQ(channel.State(), audio::StreamState::Ended);
    EXPECT_TRUE(AllZero(after));
}

TEST(AudioStream, DetachedChannelIsNotMixed) {
    audio::StreamChannel channel;
    channel.Open(Constant(16384, 480));
    channel.Play();

    audio::Mixer mixer(48000);
    mixer.AddStreamChannel(&channel);
    mixer.RemoveStreamChannel(&channel);

    EXPECT_TRUE(AllZero(fx::RenderFrames(mixer, 240)));
    EXPECT_EQ(channel.Position(), 0);
}

TEST(AudioMixerDc2, PitchMessageBendsAnEffectVoice) {
    const auto bank = MakeBank(fx::BankSpec{});
    ASSERT_NE(bank, nullptr);
    const std::array<std::uint8_t, audio::kHsMsgBytes> bend_up{0xFD, 0x02, 0x00, 60, 1, 0x7F, 0x7F};
    const std::array<std::uint8_t, audio::kHsMsgBytes> centre{0xFD, 0x02, 0x00, 60, 1, 0x00, 0x40};

    audio::Mixer plain(48000);
    Prepare(plain, bank);
    KeyOn(plain, 60);
    const int base = Crossings(plain);

    audio::Mixer raised(48000);
    Prepare(raised, bank);
    KeyOn(raised, 60);
    raised.HsMessage(kPort, bend_up);
    const int bent = Crossings(raised);

    audio::Mixer untouched(48000);
    Prepare(untouched, bank);
    KeyOn(untouched, 60);
    untouched.HsMessage(kPort, centre);
    const int centred = Crossings(untouched);

    EXPECT_GT(base, 100);
    const double expected = std::exp2(2.0 / 12.0);
    EXPECT_NEAR(static_cast<double>(bent) / base, expected, 0.05 * expected);
    EXPECT_EQ(centred, base);
}

TEST(AudioMixerDc2, BankSelectPicksAnExtraBank) {
    const auto low = MakeBank(fx::BankSpec{});
    fx::BankSpec octave_down_spec;
    octave_down_spec.base_note = 72;
    const auto octave_down = MakeBank(octave_down_spec);
    ASSERT_NE(low, nullptr);
    ASSERT_NE(octave_down, nullptr);

    audio::Mixer reference(48000);
    Prepare(reference, low);
    KeyOn(reference, 60);
    const int low_count = Crossings(reference);

    audio::Mixer selected(48000);
    selected.BindBank(kPort, low);
    selected.BindBank(kPort, 1, octave_down);
    selected.SetVolume(kPort, audio::kFullPortVolume);
    selected.ShortMessage(kPort, (1u << 16) | 0xB0);
    selected.ShortMessage(kPort, 0xC0);
    KeyOn(selected, 60);
    const int selected_count = Crossings(selected);

    audio::Mixer unselected(48000);
    unselected.BindBank(kPort, low);
    unselected.BindBank(kPort, 1, octave_down);
    unselected.SetVolume(kPort, audio::kFullPortVolume);
    unselected.ShortMessage(kPort, 0xC0);
    KeyOn(unselected, 60);
    EXPECT_EQ(Crossings(unselected), low_count);

    EXPECT_LT(selected_count, low_count);
    EXPECT_NEAR(static_cast<double>(selected_count) / low_count, 0.5, 0.025);
}

TEST(AudioMixerDc2, SelectingAnUnboundBankPlaysBankZero) {
    const auto bank = MakeBank(fx::BankSpec{});
    ASSERT_NE(bank, nullptr);

    audio::Mixer reference(48000);
    Prepare(reference, bank);
    KeyOn(reference, 60);
    const int expected = Crossings(reference);
    EXPECT_GT(expected, 100);

    audio::Mixer single(48000);
    Prepare(single, bank);
    single.ShortMessage(kPort, (1u << 16) | 0xB0);
    single.ShortMessage(kPort, 0xC0);
    KeyOn(single, 60);
    EXPECT_EQ(Crossings(single), expected);
}

TEST(AudioMixerDc2, CoreVolumeScalesItsVoices) {
    const auto bank = MakeBank(fx::BankSpec{});
    ASSERT_NE(bank, nullptr);

    audio::Mixer full(48000);
    Prepare(full, bank);
    KeyOn(full, 60);
    const auto full_out = fx::RenderFrames(full, kFrames);

    audio::Mixer half(48000);
    Prepare(half, bank);
    half.SetCoreVolume(0, 0.5f);
    half.SetCoreVolume(1, 0.5f);
    KeyOn(half, 60);
    const auto half_out = fx::RenderFrames(half, kFrames);
    EXPECT_NEAR(fx::Rms(half_out) / fx::Rms(full_out), 0.5, 0.01);

    audio::Mixer restored(48000);
    Prepare(restored, bank);
    restored.SetCoreVolume(0, 0.5f);
    restored.SetCoreVolume(1, 0.5f);
    restored.SetCoreVolume(0, 1.0f);
    restored.SetCoreVolume(1, 1.0f);
    KeyOn(restored, 60);
    EXPECT_TRUE(fx::RenderFrames(restored, kFrames) == full_out);
}

TEST(AudioMixerDc2, SequenceNotePlays) {
    const auto bank = MakeBank(fx::BankSpec{});
    ASSERT_NE(bank, nullptr);
    const auto bytes = fx::MakeSq(60, 100, 480);
    const auto file = audio::SqFile::Create(bytes);
    ASSERT_NE(file, nullptr);

    audio::Mixer mixer(48000);
    mixer.BindBank(kPort, bank);
    mixer.SetSequence(kPort, file);
    mixer.SetVolume(kPort, audio::kFullPortVolume);
    mixer.Rewind(kPort, 0);
    mixer.Play(kPort);

    EXPECT_GT(fx::Rms(fx::RenderFrames(mixer, kFrames)), 0.01);
}
