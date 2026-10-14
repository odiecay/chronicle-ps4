#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "audio/mixer.hpp"
#include "audio/pcm.hpp"
#include "audio/soundtrack.hpp"
#include "platform/settings.hpp"
#include "platform/paths.hpp"
#include "platform_fixture.hpp"

namespace {

// A 16-bit mono WAV of the samples at the rate, in a file of its own.
std::filesystem::path WriteWav(const std::string &name, const std::vector<std::int16_t> &samples, std::uint32_t rate) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream               out(path, std::ios::binary);
    const auto                  put32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };
    const auto                  put16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char *>(&v), 2); };
    const std::uint32_t         bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4);
    put32(36 + bytes);
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(1);
    put16(1);
    put32(rate);
    put32(rate * 2);
    put16(2);
    put16(16);
    out.write("data", 4);
    put32(bytes);
    out.write(reinterpret_cast<const char *>(samples.data()), bytes);
    return path;
}

std::shared_ptr<audio::PcmTrack> Track(std::vector<std::int16_t> mono, int rate) {
    auto track = std::make_shared<audio::PcmTrack>();
    for (std::int16_t s : mono) {
        track->samples.push_back(s);
        track->samples.push_back(s);
    }
    track->rate = rate;
    return track;
}

} // namespace

TEST(AudioSoundtrack, DecodesAWavToStereoAtItsOwnRate) {
    const auto  path = WriteWav("dc_soundtrack_test.wav", {100, -200, 300, -400}, 8000);
    std::string error;
    auto        track = audio::LoadPcmTrack(path, error);
    ASSERT_TRUE(track != nullptr) << error;
    EXPECT_EQ(track->rate, 8000);
    ASSERT_EQ(track->Frames(), 4);
    EXPECT_EQ(track->samples[0], 100);
    EXPECT_EQ(track->samples[1], 100);
    EXPECT_EQ(track->samples[6], -400);
    std::filesystem::remove(path);
}

TEST(AudioSoundtrack, RefusesWhatItCannotDecode) {
    std::string error;
    EXPECT_TRUE(audio::LoadPcmTrack(std::filesystem::temp_directory_path() / "dc_no_such_file.wav", error) == nullptr);
    EXPECT_FALSE(error.empty());
    const auto text = std::filesystem::temp_directory_path() / "dc_soundtrack_test.mp3";
    std::ofstream(text) << "not audio";
    error.clear();
    EXPECT_TRUE(audio::LoadPcmTrack(text, error) == nullptr);
    EXPECT_FALSE(error.empty());
    std::filesystem::remove(text);
}

TEST(AudioSoundtrack, CreatesMissingMappingAndPreservesExistingOne) {
    const auto base = std::filesystem::temp_directory_path() /
                      ("dc_soundtrack_create_" + std::to_string(dc::test::ProcessId()));
    std::filesystem::remove_all(base);
    PathsSetSaveRoot(base / "save");
    ASSERT_TRUE(audio::SoundtrackEnsure());
    const auto         path = base / "soundtrack/soundtrack.json";
    std::ifstream      created(path);
    std::ostringstream text;
    text << created.rdbuf();
    EXPECT_EQ(text.str(), "{\n  \"version\": 1,\n  \"overrides\": {}\n}\n");
    created.close();
    std::ofstream(path) << "custom mapping\n";
    ASSERT_TRUE(audio::SoundtrackEnsure());
    std::ifstream      existing(path);
    std::ostringstream preserved;
    preserved << existing.rdbuf();
    EXPECT_EQ(preserved.str(), "custom mapping\n");
    existing.close();
    std::filesystem::remove_all(base);
}

TEST(AudioSoundtrack, ReadsVersionedOverridesBesideSave) {
    const auto base = std::filesystem::temp_directory_path() /
                      ("dc_soundtrack_mapping_" + std::to_string(dc::test::ProcessId()));
    const auto save = base / "save";
    const auto soundtrack = base / "soundtrack";
    std::filesystem::create_directories(soundtrack);
    PathsSetSaveRoot(save);
    HostSettings config;
    config.soundtrack = true;
    ASSERT_TRUE(SettingsChange(config));
    const auto recording = WriteWav("dc_soundtrack_mapping.wav", {100, -200, 300, -400}, 8000);
    std::filesystem::copy_file(recording, soundtrack / "recording.wav",
                               std::filesystem::copy_options::overwrite_existing);
    std::ofstream(soundtrack / "soundtrack.json") << R"({
        "version": 1,
        "overrides": {
            "e01a_a.sq": {"file": "recording.wav", "gain": 0.5, "loop": false,
                            "loop_start": 0.000125, "loop_end": 0.000375}
        }
    })";
    audio::SoundtrackReload();
    auto track = audio::SoundtrackFor("e01a_a.sq");
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->Frames(), 4);
    EXPECT_FLOAT_EQ(track->gain, 0.5f);
    EXPECT_FALSE(track->loop);
    EXPECT_EQ(track->loop_start, 1);
    EXPECT_EQ(track->loop_end, 3);
    EXPECT_EQ(audio::SoundtrackFor("missing.sq"), nullptr);
    std::filesystem::remove(recording);
    std::filesystem::remove_all(base);
}

TEST(AudioSoundtrack, RejectsUnsupportedMappingVersion) {
    const auto base = std::filesystem::temp_directory_path() /
                      ("dc_soundtrack_version_" + std::to_string(dc::test::ProcessId()));
    std::filesystem::create_directories(base / "soundtrack");
    PathsSetSaveRoot(base / "save");
    HostSettings config;
    config.soundtrack = true;
    ASSERT_TRUE(SettingsChange(config));
    std::ofstream(base / "soundtrack/soundtrack.json") << R"({"version":2,"overrides":{"song.sq":{"file":"missing.wav"}}})";
    audio::SoundtrackReload();
    EXPECT_EQ(audio::SoundtrackFor("song.sq"), nullptr);
    std::filesystem::remove_all(base);
}

TEST(AudioSoundtrack, AStreamStandsInForThePortsSequence) {
    audio::Mixer mixer(48000);
    mixer.SetVolume(0, audio::kFullPortVolume);
    mixer.SetStream(0, Track(std::vector<std::int16_t>(200, 16384), 48000));
    EXPECT_FALSE(mixer.IsPlaying(0));
    mixer.Play(0);
    EXPECT_TRUE(mixer.IsPlaying(0));
    std::vector<float> out(2 * 50);
    mixer.Render(out.data(), 50);
    EXPECT_NEAR(out[0], 0.5f, 0.01f);
    EXPECT_NEAR(out[99], 0.5f, 0.01f);
    mixer.Stop(0);
    EXPECT_FALSE(mixer.IsPlaying(0));
}

TEST(AudioSoundtrack, ThePortsVolumeScalesTheStream) {
    audio::Mixer mixer(48000);
    mixer.SetVolume(0, audio::kFullPortVolume / 2);
    mixer.SetStream(0, Track(std::vector<std::int16_t>(100, 16384), 48000));
    mixer.Play(0);
    std::vector<float> out(2 * 10);
    mixer.Render(out.data(), 10);
    EXPECT_NEAR(out[0], 0.25f, 0.01f);
}

TEST(AudioSoundtrack, StartBeginsPartwayIn) {
    std::vector<std::int16_t> samples(100, 0);
    std::fill(samples.begin() + 50, samples.end(), 16384);
    auto track = Track(samples, 48000);
    track->start = 50;
    audio::Mixer mixer(48000);
    mixer.SetVolume(0, audio::kFullPortVolume);
    mixer.SetStream(0, track);
    mixer.Rewind(0, 0);
    mixer.Play(0);
    std::vector<float> out(2 * 10);
    mixer.Render(out.data(), 10);
    EXPECT_NEAR(out[0], 0.5f, 0.01f);
}

TEST(AudioSoundtrack, ALoopingStreamRepeatsFromItsLoopStartAndAnotherEnds) {
    std::vector<std::int16_t> samples(40, 0);
    std::fill(samples.begin() + 20, samples.end(), 16384);
    auto track = Track(samples, 48000);
    track->loop = true;
    track->loop_start = 20;
    audio::Mixer mixer(48000);
    mixer.SetVolume(0, audio::kFullPortVolume);
    mixer.SetStream(0, track);
    mixer.Play(0);
    std::vector<float> out(2 * 100);
    mixer.Render(out.data(), 100);
    EXPECT_NEAR(out[2 * 99], 0.5f, 0.01f); // long past the end, still on the loop
    EXPECT_TRUE(mixer.IsPlaying(0));

    auto once = Track(samples, 48000);
    once->loop = false;
    mixer.SetStream(1, once);
    mixer.SetVolume(1, audio::kFullPortVolume);
    mixer.Play(1);
    mixer.Render(out.data(), 100);
    EXPECT_FALSE(mixer.IsPlaying(1));
}
