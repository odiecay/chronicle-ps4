#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "gfx/context.hpp"
#include "gfx_fixture.hpp"
#include "platform/window.hpp"

using namespace dc::test;

namespace {

constexpr const char *kLayerName = "VK_LAYER_KHRONOS_validation";
#ifdef __linux__
constexpr const char *kLayerMissing =
    "the image must install vulkan-validationlayers: VK_LAYER_KHRONOS_validation is missing";
#endif

bool LayerInstalled() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties &layer) {
        return std::strcmp(layer.layerName, kLayerName) == 0;
    });
}

} // namespace

TEST(GfxValidation, LayerIsInstalled) {
#ifdef __linux__
    EXPECT_TRUE(LayerInstalled()) << kLayerMissing;
#else
    if (!LayerInstalled()) {
        GTEST_SKIP() << "no Vulkan SDK on this host: " << kLayerName << " is missing";
    }
#endif
}

TEST(GfxValidation, DeliberateMisuseIsCaught) {
#ifdef NDEBUG
    GTEST_SKIP() << "the renderer compiles validation out of release builds";
#endif
#ifdef __linux__
    ASSERT_TRUE(LayerInstalled()) << kLayerMissing;
#else
    if (!LayerInstalled()) {
        GTEST_SKIP() << "no Vulkan SDK on this host: " << kLayerName << " is missing";
    }
#endif

    // Duplicates GfxFixture's setup: its destructor fails on any message, and this test needs one.
    const GfxOptions options{};
    SetEnv("VK_LAYER_VALIDATE_SYNC", "1", 0);
    WindowConfig window{640, 480, true};
    window.vulkan = !options.offscreen;
    WindowInit(window);
    gfx::RendererConfig config;
    config.app_name = "Kazarov tests";
    config.logical_width = 640.0f;
    config.logical_height = 480.0f;
    config.display_aspect = 4.0f / 3.0f;
    config.validation = true;
    config.render_scale = 1.0f;
    config.dynamic_color_write_mask = options.dynamic_color_write_mask;
    config.offscreen = options.offscreen;
    config.triangle_fans = options.triangle_fans;
    config.separate_stencil_masks = options.separate_stencil_masks;
    config.prog0_meshes = options.prog0_meshes;
    config.pipeline_cache = options.pipeline_cache;
    gfx::RendererInit(WindowHandle(), config);

    // EXPECT, not ASSERT: a fatal failure here would skip the shutdown below.
    EXPECT_EQ(gfx::ValidationMessageCount(), 0u);

    // A zero size is the single violation (VUID-VkBufferCreateInfo-size-00912); usage is set so
    // that no other buffer VUID fires alongside it.
    VkBufferCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = 0;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    vkCreateBuffer(gfx::detail::g.device, &info, nullptr, &buffer);
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(gfx::detail::g.device, buffer, nullptr);
    }

    EXPECT_GT(gfx::ValidationMessageCount(), 0u);

    gfx::RendererShutdown();
    WindowShutdown();
}
