#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include "data_fixture.hpp"
#include "dataread.hpp"
#include "mod_assets.hpp"
#include "platform/arena.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"

namespace {
std::filesystem::path InstallGrowthPack() {
    using namespace datafix;
    const auto root = TempDir("mod_asset_io");
    const auto base = root / "data/test.bin", replacement = root / "mods/growth/files/test.bin";
    WriteBytes(base, Bytes(2048, 1));
    WriteBytes(replacement, Bytes(4096, 2));
    const nlohmann::json manifest{{"format",2},{"id","growth"},{"name","Growth"},{"version","1"},
        {"game","dark-cloud"},{"type","asset-pack"},{"arena_checked_growth",true},
        {"files",nlohmann::json::array({{{"path","test.bin"},{"source","files/test.bin"},{"original_size",2048},{"size",4096},
            {"original_fnv1a64",ModFileFingerprint(base)},{"replacement_fnv1a64",ModFileFingerprint(replacement)}}})}};
    std::ofstream(root/"mods/growth/mod.json") << manifest.dump();
    PathsSetDataRoot(root/"data"); PathsSetSaveRoot(root/"save");
    Config config;
    config.mods_configured = true; config.enabled_mods = {"growth"};
    EXPECT_TRUE(ConfigChange(config));
    InitializeMods();
    return root;
}
}

TEST(ModAssetIO, RealLoaderBoundsResizedReplacementsAndRestoresBase) {
    const auto root = InstallGrowthPack();
    static char key;
    auto *buffer = ArenaBlock(&key, 8192);
    std::memset(buffer, 0x7f, 8192);
    char path[] = "test.bin";
    int size = 0;
    ASSERT_EQ(LoadFile2(path, buffer, &size, 0), 1);
    ASSERT_EQ(size, 4096);
    ASSERT_EQ(buffer[0], 2);
    ASSERT_EQ(buffer[4095], 2);
    ASSERT_EQ(buffer[4096], 0x7f);
    ASSERT_EQ(std::filesystem::file_size(root/"data/test.bin"), 2048u);
    Config config = ConfigGet(); config.enabled_mods.clear();
    ASSERT_TRUE(ConfigChange(config));
    InitializeMods(); // a controlled startup reinitialization, not in-game hot reload
    ASSERT_EQ(LoadFile2(path, buffer, &size, 0), 1);
    ASSERT_EQ(size, 2048);
    ASSERT_EQ(buffer[0], 1);
    std::filesystem::remove_all(root);
}

TEST(ModAssetIO, GrowthIntoUnrecognizedOrTooSmallBufferIsRefused) {
    const auto root = InstallGrowthPack();
    alignas(64) unsigned char original_capacity[2048]{};
    char path[] = "test.bin";
    EXPECT_DEATH(LoadFile2(path, original_capacity, nullptr, 0), "replacement size/destination contract failed");
    static char key;
    auto *small = ArenaBlock(&key, 2048);
    EXPECT_DEATH(LoadFile2(path, small, nullptr, 0), "replacement size/destination contract failed");
    std::filesystem::remove_all(root);
}
