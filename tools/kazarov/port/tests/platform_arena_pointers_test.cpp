#include <gtest/gtest.h>

#include <cstdint>

#include "platform/arena.hpp"

namespace {

int g_image_object;

} // namespace

TEST(PlatformArenaPointers, WidenNearRecoversTheWindow) {
    constexpr std::uintptr_t kGiB = std::uintptr_t{1} << 30;
    // Below 4 GiB, as Linux links it.
    ASSERT_TRUE(WidenNear(0x400000, 0x00612340u) == 0x612340);
    ASSERT_TRUE(WidenNear(0x400000, 0xF0000000u) == 0xF0000000u);
    // A PIE or Mach-O image high up, the object on either side of the anchor.
    std::uintptr_t anchor = 0x555555554000;
    ASSERT_TRUE(WidenNear(anchor, static_cast<std::uint32_t>(anchor + 0x123456)) == anchor + 0x123456);
    ASSERT_TRUE(WidenNear(anchor, static_cast<std::uint32_t>(anchor - 0x5000)) == anchor - 0x5000);
    // An image straddling a 4 GiB boundary.
    std::uintptr_t straddle = 0x7fff00000000 - 0x1000;
    ASSERT_TRUE(WidenNear(straddle, static_cast<std::uint32_t>(straddle + 0x3000)) == straddle + 0x3000);
    ASSERT_TRUE(WidenNear(straddle + 0x3000, static_cast<std::uint32_t>(straddle)) == straddle);
    ASSERT_TRUE(WidenNear(0x100000000 + kGiB, 0x10u) == 0x100000010);
}

TEST(PlatformArenaPointers, ImagePointerRoundTrips) {
    auto truncated = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(&g_image_object));
    ASSERT_TRUE(PortImagePointer(truncated) == &g_image_object);
    ASSERT_TRUE(PortImagePointer(0) == nullptr);
    static const char literal[] = "image";
    ASSERT_TRUE(PortImagePointer(static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(literal))) == literal);
}
