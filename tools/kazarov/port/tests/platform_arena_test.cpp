#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/arena.hpp"
#include "platform/memory.hpp"
#include "platform_fixture.hpp"

namespace {

bool AllZero(const unsigned char *bytes, std::size_t count) {
    return std::all_of(bytes, bytes + count, [](unsigned char byte) { return byte == 0; });
}

} // namespace

TEST(PlatformArenaBlock, HandsOutAZeroedAlignedBlock) {
    static char key;
    unsigned char *base = ArenaBlock(&key, 1000);
    ASSERT_TRUE((reinterpret_cast<std::uintptr_t>(base) & 63) == 0);
    ASSERT_TRUE(AllZero(base, 1000));
    base[999] = 1;

    static char leaded_key;
    unsigned char *leaded = ArenaBlock(&leaded_key, 1000, 0x1000);
    ASSERT_TRUE((reinterpret_cast<std::uintptr_t>(leaded) & 63) == 0);
    leaded[-static_cast<std::ptrdiff_t>(0x1000)] = 1;
    leaded[-1] = 1;
    leaded[999] = 1;
}

TEST(PlatformArenaBlock, ReusesTheBlockOfAnOwner) {
    static char key;
    unsigned char *first = ArenaBlock(&key, 1000);
    first[0] = 0x77;
    ASSERT_TRUE(ArenaBlock(&key, 500) == first);
    ASSERT_TRUE(first[0] == 0x77);
}

TEST(PlatformArenaBlock, RemapsAnOwnerThatOutgrowsItsBlock) {
    static char key;
    constexpr std::size_t kGrown = std::size_t{1} << 20;
    unsigned char *small = ArenaBlock(&key, 1000);
    std::memset(small, 0x5A, 1000);
    unsigned char *grown = ArenaBlock(&key, kGrown);
    grown[kGrown - 1] = 1;
    ASSERT_TRUE(ArenaBlock(&key, kGrown) == grown);
    ASSERT_TRUE(AllZero(small, 1000));
}

TEST(PlatformArenaBlock, ClearAllZeroesEveryBlock) {
    static char first_key;
    static char second_key;
    unsigned char *first = ArenaBlock(&first_key, 1000);
    unsigned char *second = ArenaBlock(&second_key, 1000);
    std::memset(first, 0xA5, 1000);
    std::memset(second, 0xA5, 1000);
    ArenaClearAll();
    ASSERT_TRUE(AllZero(first, 1000));
    ASSERT_TRUE(AllZero(second, 1000));
    ASSERT_TRUE(ArenaBlock(&first_key, 1000) == first);
    ASSERT_TRUE(ArenaBlock(&second_key, 1000) == second);
}

TEST(PlatformArenaBlock, GuardPageFollowsTheBlock) {
    static char key;
    constexpr std::size_t kBytes = std::size_t{4096} * 16;
    unsigned char *base = ArenaBlock(&key, kBytes);
    base[kBytes - 1] = 1;
    KZ_ASSERT_FAULT([&] { base[kBytes] = 1; });
}

TEST(PlatformArenaBlock, OverflowNamesTheArena) {
    EXPECT_DEATH(ArenaOverflow("EdMenu", 9, 8), "arena EdMenu: allocation overflow, 9 of 8 quadwords");
}

TEST(PlatformArenaBlock, OverflowOfAnUnnamedArenaGivesItsAddress) {
    static char key;
    char address[64];
    std::snprintf(address, sizeof address, "%p", static_cast<void *>(&key));
    const std::string expected = std::string("arena ") + address + ": allocation overflow, 9 of 8 quadwords";
    EXPECT_DEATH(ArenaOverflow(static_cast<const void *>(&key), 9, 8), expected.c_str());
}

TEST(PlatformArenaBlock, ContainsOnlyItsBlocks) {
    static char key;
    static char lead_key;
    static char other;
    unsigned char *base = ArenaBlock(&key, 1000);
    unsigned char *leaded = ArenaBlock(&lead_key, 1000, 0x1000);
    int            local = 0;
    ASSERT_TRUE(ArenaContains(base));
    ASSERT_TRUE(ArenaContains(base + 999));
    ASSERT_TRUE(ArenaContains(leaded - 0x1000));
    ASSERT_TRUE(ArenaContains(leaded + 999));
    EXPECT_FALSE(ArenaContains(&local));
    EXPECT_FALSE(ArenaContains(&other));
}

TEST(PlatformArenaBlock, RequiresABlockAbove4GiB) {
#ifdef NDEBUG
    GTEST_SKIP() << "the above-4 GiB check is debug-only";
#else
    const void *low[] = {reinterpret_cast<const void *>(std::uintptr_t{0x10000})};
    EXPECT_DEATH(ArenaRequireOneAbove4GiB(low, 1), "none lies above 4 GiB, where a truncated pointer would show");
    const void *mixed[] = {
        reinterpret_cast<const void *>(std::uintptr_t{0x10000}),
        reinterpret_cast<const void *>((std::uintptr_t{1} << 32) | 0x1000),
    };
    ArenaRequireOneAbove4GiB(mixed, 2);
#endif
}

TEST(PlatformArenaBlock, DebugBlocksLieAbove4GiB) {
#ifdef NDEBUG
    GTEST_SKIP() << "the above-4 GiB check is debug-only";
#else
    static char key;
    EXPECT_TRUE(IsAbove4GiB(ArenaBlock(&key, 1 << 20)));
#endif
}

TEST(PlatformArenaBlock, HeadroomIsFourTimes) {
    EXPECT_EQ(kArenaHeadroom, 4);
}
