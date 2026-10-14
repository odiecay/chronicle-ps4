#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include "gfx/tim2.hpp"
#include "gfx_fixture.hpp"

namespace {

namespace tim2 = gfx::tim2;

std::array<uint8_t, 4> Texel(const tim2::Image &image, uint32_t level, size_t x, size_t y) {
    const tim2::Level &l = image.levels[level];
    const uint8_t *p = &l.pixels[(y * l.width + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

void PutLe(std::vector<uint8_t> &buf, size_t offset, uint64_t value, size_t bytes) {
    if (buf.size() < offset + bytes) {
        buf.resize(offset + bytes);
    }
    for (size_t i = 0; i < bytes; i++) {
        buf[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

std::vector<uint8_t> SolidCt32(size_t texels, uint8_t red) {
    std::vector<uint8_t> out(texels * 4);
    for (size_t t = 0; t < texels; t++) {
        out[t * 4] = red;
        out[t * 4 + 1] = 0;
        out[t * 4 + 2] = 0;
        out[t * 4 + 3] = 0x80;
    }
    return out;
}

struct PictureSpec {
    uint8_t              image_type = 0;
    uint16_t             width = 0;
    uint16_t             height = 0;
    std::vector<uint32_t> level_sizes; // more than one selects the mipmap header
    std::vector<uint8_t> pixels;
    uint8_t              clut_type = 0;
    uint16_t             clut_colors = 0;
    std::vector<uint8_t> clut;
    uint64_t             tex0 = 0;
    uint64_t             tex1 = 0;
};

std::vector<uint8_t> BuildPicture(const PictureSpec &spec) {
    const bool   mipmapped = spec.level_sizes.size() > 1;
    const size_t header = mipmapped ? 0x60 : 0x30;
    std::vector<uint8_t> out(header, 0);
    PutLe(out, 0, header + spec.pixels.size() + spec.clut.size(), 4);
    PutLe(out, 4, spec.clut.size(), 4);
    PutLe(out, 8, spec.pixels.size(), 4);
    PutLe(out, 0xC, header, 2);
    PutLe(out, 0xE, spec.clut_colors, 2);
    PutLe(out, 0x11, spec.level_sizes.size(), 1);
    PutLe(out, 0x12, spec.clut_type, 1);
    PutLe(out, 0x13, spec.image_type, 1);
    PutLe(out, 0x14, spec.width, 2);
    PutLe(out, 0x16, spec.height, 2);
    PutLe(out, 0x18, spec.tex0, 8);
    PutLe(out, 0x20, spec.tex1, 8);
    if (mipmapped) {
        for (size_t i = 0; i < spec.level_sizes.size() && i < 8; i++) {
            PutLe(out, 0x40 + 4 * i, spec.level_sizes[i], 4);
        }
    }
    out.insert(out.end(), spec.pixels.begin(), spec.pixels.end());
    out.insert(out.end(), spec.clut.begin(), spec.clut.end());
    return out;
}

std::vector<uint8_t> MakeFile(uint8_t format, const std::vector<std::vector<uint8_t>> &pictures) {
    const size_t first = format == 0 ? 0x10 : 0x80;
    std::vector<uint8_t> file(first, 0);
    const uint8_t        magic[4] = {'T', 'I', 'M', '2'};
    std::copy(std::begin(magic), std::end(magic), file.begin());
    file[4] = 3;
    file[5] = format;
    PutLe(file, 6, pictures.size(), 2);
    size_t at = first;
    for (const std::vector<uint8_t> &picture : pictures) {
        file.resize(at + picture.size());
        std::copy(picture.begin(), picture.end(), file.begin() + static_cast<std::ptrdiff_t>(at));
        at += picture.size();
    }
    return file;
}

} // namespace

TEST(GfxTim2, Ct32AlphaConverted) {
    const std::vector<uint8_t> texels = {10, 20, 30, 0x80, 1, 2, 3, 0x40, 4, 5, 6, 0x00, 7, 8, 9, 0xFF};
    tim2::Source               source;
    source.psm = tim2::Psm::Ct32;
    source.width = 4;
    source.height = 1;
    source.levels[0] = texels.data();
    std::optional<tim2::Image> image = tim2::Decode(source);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->format, gfx::TextureFormat::Rgba8);
    EXPECT_TRUE(image->has_alpha);
    EXPECT_EQ(Texel(*image, 0, 0, 0), (std::array<uint8_t, 4>{10, 20, 30, 0xFF}));
    EXPECT_EQ(Texel(*image, 0, 1, 0), (std::array<uint8_t, 4>{1, 2, 3, 0x80}));
    EXPECT_EQ(Texel(*image, 0, 2, 0), (std::array<uint8_t, 4>{4, 5, 6, 0x00}));
    EXPECT_EQ(Texel(*image, 0, 3, 0), (std::array<uint8_t, 4>{7, 8, 9, 0xFF}));
}

TEST(GfxTim2, Ct24OpaqueWithoutAlpha) {
    const std::vector<uint8_t> texels = {1, 2, 3, 4, 5, 6};
    tim2::Source               source;
    source.psm = tim2::Psm::Ct24;
    source.width = 2;
    source.height = 1;
    source.levels[0] = texels.data();
    std::optional<tim2::Image> image = tim2::Decode(source);
    ASSERT_TRUE(image.has_value());
    EXPECT_FALSE(image->has_alpha);
    EXPECT_EQ(Texel(*image, 0, 0, 0), (std::array<uint8_t, 4>{1, 2, 3, 0xFF}));
    EXPECT_EQ(Texel(*image, 0, 1, 0), (std::array<uint8_t, 4>{4, 5, 6, 0xFF}));
}

TEST(GfxTim2, Ct16WidensAndAppliesTexa) {
    auto decode = [](uint16_t value, const tim2::Texa &texa) {
        const std::vector<uint8_t> bytes = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
        tim2::Source               source;
        source.psm = tim2::Psm::Ct16;
        source.width = 1;
        source.height = 1;
        source.levels[0] = bytes.data();
        source.texa = texa;
        std::optional<tim2::Image> image = tim2::Decode(source);
        EXPECT_TRUE(image.has_value());
        return image ? Texel(*image, 0, 0, 0) : std::array<uint8_t, 4>{};
    };
    const tim2::Texa defaults{};

    EXPECT_EQ(decode(0x7FFF, defaults), (std::array<uint8_t, 4>{248, 248, 248, 0xFF}));
    EXPECT_EQ(decode(0x0400, defaults), (std::array<uint8_t, 4>{0, 0, 8, 0xFF}));
    EXPECT_EQ(decode(0x0020, defaults), (std::array<uint8_t, 4>{0, 8, 0, 0xFF}));
    EXPECT_EQ(decode(0x0001, defaults), (std::array<uint8_t, 4>{8, 0, 0, 0xFF}));
    EXPECT_EQ(decode(0x8000, defaults), (std::array<uint8_t, 4>{0, 0, 0, 0xFF}));
    EXPECT_EQ(decode(0x0000, defaults), (std::array<uint8_t, 4>{0, 0, 0, 0}));

    tim2::Texa no_aem;
    no_aem.aem = false;
    EXPECT_EQ(decode(0x0000, no_aem), (std::array<uint8_t, 4>{0, 0, 0, 0xFF}));

    tim2::Texa low_ta0;
    low_ta0.ta0 = 0x40;
    EXPECT_EQ(decode(0x7FFF, low_ta0), (std::array<uint8_t, 4>{248, 248, 248, 0x80}));

    tim2::Texa low_ta1;
    low_ta1.ta1 = 0x40;
    EXPECT_EQ(decode(0x8000, low_ta1), (std::array<uint8_t, 4>{0, 0, 0, 0x80}));
}

TEST(GfxTim2, T8ClutCsm1) {
    for (uint32_t i = 0; i < 256; i++) {
        ASSERT_EQ(tim2::ClutCsm1(tim2::ClutCsm1(i)), i);
    }
    EXPECT_EQ(tim2::ClutCsm1(8), 16u);
    EXPECT_EQ(tim2::ClutCsm1(16), 8u);
    EXPECT_EQ(tim2::ClutCsm1(0x18), 0x18u);

    // File entry p is red p, so its palette slot's red names where it landed.
    std::vector<uint8_t> clut(256 * 4);
    for (size_t p = 0; p < 256; p++) {
        clut[p * 4] = static_cast<uint8_t>(p);
        clut[p * 4 + 1] = 0;
        clut[p * 4 + 2] = 0;
        clut[p * 4 + 3] = 0x80;
    }
    std::vector<uint8_t> indices(16 * 16);
    for (size_t i = 0; i < indices.size(); i++) {
        indices[i] = static_cast<uint8_t>(i);
    }
    tim2::Source source;
    source.psm = tim2::Psm::T8;
    source.width = 16;
    source.height = 16;
    source.levels[0] = indices.data();
    source.clut = clut.data();

    std::optional<tim2::Image> swizzled_order = tim2::Decode(source);
    ASSERT_TRUE(swizzled_order.has_value());
    EXPECT_EQ(swizzled_order->format, gfx::TextureFormat::Index8);
    for (uint32_t p = 0; p < 256; p++) {
        EXPECT_EQ(swizzled_order->palette[tim2::ClutCsm1(p)] & 0xFFu, p);
        EXPECT_EQ(swizzled_order->palette[tim2::ClutCsm1(p)] >> 24, 0xFFu);
    }

    source.csm1 = false;
    std::optional<tim2::Image> linear_order = tim2::Decode(source);
    ASSERT_TRUE(linear_order.has_value());
    for (uint32_t p = 0; p < 256; p++) {
        EXPECT_EQ(linear_order->palette[p] & 0xFFu, p);
    }

    std::vector<uint8_t> clut16(256 * 2);
    for (size_t p = 0; p < 256; p++) {
        const auto value = static_cast<uint16_t>(0x8000u | (p & 0x1Fu));
        clut16[p * 2] = static_cast<uint8_t>(value);
        clut16[p * 2 + 1] = static_cast<uint8_t>(value >> 8);
    }
    source.clut = clut16.data();
    source.clut16 = true;
    std::optional<tim2::Image> expanded = tim2::Decode(source);
    ASSERT_TRUE(expanded.has_value());
    for (uint32_t p = 0; p < 256; p++) {
        EXPECT_EQ(expanded->palette[p] & 0xFFu, (p & 0x1Fu) << 3);
        EXPECT_EQ(expanded->palette[p] >> 24, 0xFFu);
    }
}

TEST(GfxTim2, T4LowNibbleFirst) {
    const std::vector<uint8_t> texels = {0x21, 0x0F};
    std::vector<uint8_t>       clut(16 * 4);
    for (size_t p = 0; p < 16; p++) {
        clut[p * 4] = static_cast<uint8_t>(p);
        clut[p * 4 + 1] = 0;
        clut[p * 4 + 2] = 0;
        clut[p * 4 + 3] = 0x80;
    }
    tim2::Source source;
    source.psm = tim2::Psm::T4;
    source.width = 4;
    source.height = 1;
    source.levels[0] = texels.data();
    source.clut = clut.data();
    std::optional<tim2::Image> image = tim2::Decode(source);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->format, gfx::TextureFormat::Index8);
    EXPECT_EQ(image->levels[0].pixels, (std::vector<uint8_t>{1, 2, 15, 0}));
    for (uint32_t p = 0; p < 16; p++) {
        EXPECT_EQ(image->palette[p] & 0xFFu, p);
    }
    EXPECT_EQ(image->palette[16], 0u);
}

TEST(GfxTim2, DecodePaletteWithoutLevels) {
    std::vector<uint8_t> clut(256 * 4);
    for (size_t p = 0; p < 256; p++) {
        clut[p * 4] = static_cast<uint8_t>(p);
        clut[p * 4 + 1] = static_cast<uint8_t>(255 - p);
        clut[p * 4 + 2] = 0x10;
        clut[p * 4 + 3] = 0x80;
    }
    tim2::Source source;
    source.psm = tim2::Psm::T8;
    source.clut = clut.data();
    const std::array<uint32_t, 256> palette = tim2::DecodePalette(source);
    for (uint32_t p = 0; p < 256; p++) {
        EXPECT_EQ(palette[tim2::ClutCsm1(p)] & 0xFFu, p);
        EXPECT_EQ(palette[tim2::ClutCsm1(p)] >> 24, 0xFFu);
    }

    const std::vector<uint8_t> indices(8 * 8, 0);
    source.width = 8;
    source.height = 8;
    source.levels[0] = indices.data();
    std::optional<tim2::Image> image = tim2::Decode(source);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->palette, palette);

    std::vector<uint8_t> clut16(16 * 2);
    for (uint32_t p = 0; p < 16; p++) {
        const auto value = static_cast<uint16_t>(0x8000u | (p << 5));
        clut16[p * 2] = static_cast<uint8_t>(value);
        clut16[p * 2 + 1] = static_cast<uint8_t>(value >> 8);
    }
    tim2::Source four;
    four.psm = tim2::Psm::T4;
    four.clut = clut16.data();
    four.clut16 = true;
    const std::array<uint32_t, 256> four_palette = tim2::DecodePalette(four);
    for (uint32_t p = 0; p < 256; p++) {
        EXPECT_EQ(four_palette[p], p < 16 ? ((p << 3) << 8) | 0xFF000000u : 0u) << "entry " << p;
    }
    const std::vector<uint8_t> nibbles = {0x21, 0x0F};
    four.width = 4;
    four.height = 1;
    four.levels[0] = nibbles.data();
    std::optional<tim2::Image> four_image = tim2::Decode(four);
    ASSERT_TRUE(four_image.has_value());
    EXPECT_EQ(four_image->palette, four_palette);

    tim2::Source empty;
    empty.psm = tim2::Psm::T8;
    EXPECT_EQ(tim2::DecodePalette(empty), (std::array<uint32_t, 256>{}));
    empty.psm = tim2::Psm::Ct32;
    empty.clut = clut.data();
    EXPECT_EQ(tim2::DecodePalette(empty), (std::array<uint32_t, 256>{}));
}

TEST(GfxTim2, MipLevels) {
    const std::vector<uint8_t> l0 = SolidCt32(8 * 8, 1);
    const std::vector<uint8_t> l1 = SolidCt32(4 * 4, 2);
    const std::vector<uint8_t> l2 = SolidCt32(2 * 2, 3);
    tim2::Source               source;
    source.psm = tim2::Psm::Ct32;
    source.width = 8;
    source.height = 8;
    source.levels = {l0.data(), l1.data(), l2.data(), nullptr};
    std::optional<tim2::Image> image = tim2::Decode(source);
    ASSERT_TRUE(image.has_value());
    ASSERT_EQ(image->levels.size(), 3u);
    const uint32_t sizes[3] = {8, 4, 2};
    for (uint32_t i = 0; i < 3; i++) {
        EXPECT_EQ(image->levels[i].width, sizes[i]);
        EXPECT_EQ(image->levels[i].height, sizes[i]);
        ASSERT_EQ(image->levels[i].pixels.size(), static_cast<size_t>(sizes[i]) * sizes[i] * 4);
        EXPECT_EQ(Texel(*image, i, sizes[i] - 1, sizes[i] - 1),
                  (std::array<uint8_t, 4>{static_cast<uint8_t>(i + 1), 0, 0, 0xFF}));
    }
}

TEST(GfxTim2, SwizzleRoundTrip) {
    std::mt19937 rng(0x5EEDu);
    const std::array<std::pair<uint32_t, uint32_t>, 6> sizes = {
        {{16, 16}, {32, 32}, {64, 64}, {128, 64}, {128, 128}, {256, 128}}};
    for (const auto &[width, height] : sizes) {
        const size_t         n = static_cast<size_t>(width) * height;
        std::vector<uint8_t> linear(n);
        for (uint8_t &byte : linear) {
            byte = static_cast<uint8_t>(rng());
        }
        std::vector<uint8_t> swizzled(n);
        std::vector<uint8_t> back(n);
        tim2::Swizzle8(linear.data(), swizzled.data(), width, height);
        tim2::Unswizzle8(swizzled.data(), back.data(), width, height);
        EXPECT_EQ(back, linear);

        std::vector<uint8_t> relinearized(n);
        tim2::Unswizzle8(linear.data(), swizzled.data(), width, height);
        tim2::Swizzle8(swizzled.data(), relinearized.data(), width, height);
        EXPECT_EQ(relinearized, linear);

        if (n > 4096) {
            continue;
        }
        // Each one-hot source must land on its own distinct index.
        std::vector<bool> hit(n, false);
        for (size_t i = 0; i < n; i++) {
            std::vector<uint8_t> one_hot(n, 0);
            one_hot[i] = 1;
            std::vector<uint8_t> out(n, 0);
            tim2::Swizzle8(one_hot.data(), out.data(), width, height);
            const auto ones = std::count(out.begin(), out.end(), uint8_t{1});
            ASSERT_EQ(ones, 1);
            const auto at = static_cast<size_t>(std::find(out.begin(), out.end(), uint8_t{1}) - out.begin());
            ASSERT_FALSE(hit[at]);
            hit[at] = true;
        }
    }
}

TEST(GfxTim2, SwizzleKnownPositions) {
    struct Known {
        uint32_t x;
        uint32_t y;
        size_t   index;
    };
    // Literals worked out by hand from the block formula, so a change to the formula shows here.
    const std::array<Known, 9> known = {{{0, 0, 0}, {1, 0, 4}, {8, 0, 2}, {4, 0, 16}, {0, 1, 32},
                                         {0, 2, 17}, {2, 2, 25}, {0, 3, 49}, {0, 8, 128}}};
    for (const Known &k : known) {
        std::vector<uint8_t> src(16 * 16, 0);
        src[k.y * 16 + k.x] = 0x55;
        std::vector<uint8_t> dst(16 * 16, 0);
        tim2::Swizzle8(src.data(), dst.data(), 16, 16);
        EXPECT_EQ(dst[k.index], 0x55) << "texel " << k.x << "," << k.y;
        EXPECT_EQ(std::count(dst.begin(), dst.end(), uint8_t{0x55}), 1);
    }

    std::vector<uint8_t> partial(8 * 8);
    for (size_t i = 0; i < partial.size(); i++) {
        partial[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> out(partial.size());
    tim2::Swizzle8(partial.data(), out.data(), 8, 8);
    EXPECT_EQ(out, partial);
}

TEST(GfxTim2, DecodeSwizzledT8) {
    const uint32_t       width = 32;
    const uint32_t       height = 32;
    std::vector<uint8_t> linear(width * height);
    for (size_t i = 0; i < linear.size(); i++) {
        linear[i] = static_cast<uint8_t>(i * 7);
    }
    std::vector<uint8_t> swizzled(linear.size());
    tim2::Swizzle8(linear.data(), swizzled.data(), width, height);

    tim2::Source source;
    source.psm = tim2::Psm::T8;
    source.width = width;
    source.height = height;
    source.levels[0] = swizzled.data();
    source.swizzled = true;
    std::optional<tim2::Image> image = tim2::Decode(source);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->levels[0].pixels, linear);
}

TEST(GfxTim2, ParsePictureAndFile) {
    std::vector<uint8_t> pixels_a(64);
    for (size_t i = 0; i < pixels_a.size(); i++) {
        pixels_a[i] = static_cast<uint8_t>(i);
    }
    const std::vector<uint8_t> picture_a = BuildPicture({.image_type = 3,
                                                         .width = 4,
                                                         .height = 4,
                                                         .level_sizes = {64},
                                                         .pixels = pixels_a,
                                                         .tex0 = 0x1122334455667788ull,
                                                         .tex1 = 0x0102030405060708ull});

    std::vector<uint8_t> pixels_b(80);
    for (size_t i = 0; i < pixels_b.size(); i++) {
        pixels_b[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> clut_b(256 * 4);
    for (size_t i = 0; i < clut_b.size(); i++) {
        clut_b[i] = static_cast<uint8_t>(0xC0 + i);
    }
    const std::vector<uint8_t> picture_b = BuildPicture({.image_type = 5,
                                                         .width = 8,
                                                         .height = 8,
                                                         .level_sizes = {64, 16},
                                                         .pixels = pixels_b,
                                                         .clut_type = 0x03,
                                                         .clut_colors = 256,
                                                         .clut = clut_b,
                                                         .tex0 = 0xAAAAull,
                                                         .tex1 = 0xBBBBull});

    std::vector<uint8_t> pixels_c(128, 0x5A);
    std::vector<uint8_t> clut_c(32, 0xE1);
    const std::vector<uint8_t> picture_c = BuildPicture({.image_type = 4,
                                                         .width = 16,
                                                         .height = 16,
                                                         .level_sizes = {128},
                                                         .pixels = pixels_c,
                                                         .clut_type = 0x01,
                                                         .clut_colors = 16,
                                                         .clut = clut_c});

    const std::vector<uint8_t> format0 = MakeFile(0, {picture_a, picture_b});
    std::optional<std::vector<tim2::Picture>> pictures = tim2::Parse(format0);
    ASSERT_TRUE(pictures.has_value());
    ASSERT_EQ(pictures->size(), 2u);

    const tim2::Picture &a = (*pictures)[0];
    EXPECT_EQ(a.psm, tim2::Psm::Ct32);
    EXPECT_EQ(a.width, 4u);
    EXPECT_EQ(a.height, 4u);
    EXPECT_EQ(a.level_count, 1u);
    EXPECT_EQ(a.levels[0], format0.data() + 0x10 + 0x30);
    EXPECT_EQ(a.levels[0][7], 7);
    EXPECT_EQ(a.levels[1], nullptr);
    EXPECT_EQ(a.clut, nullptr);
    EXPECT_FALSE(a.clut16);
    EXPECT_TRUE(a.clut_csm1);
    EXPECT_EQ(a.clut_colors, 0u);
    EXPECT_EQ(a.tex0, 0x1122334455667788ull);
    EXPECT_EQ(a.tex1, 0x0102030405060708ull);
    EXPECT_EQ(a.total_size, 112u);

    const tim2::Picture &b = (*pictures)[1];
    EXPECT_EQ(b.psm, tim2::Psm::T8);
    EXPECT_EQ(b.width, 8u);
    EXPECT_EQ(b.height, 8u);
    EXPECT_EQ(b.level_count, 2u);
    EXPECT_EQ(b.levels[0], format0.data() + 0x80 + 0x60);
    EXPECT_EQ(b.levels[1], format0.data() + 0x80 + 0x60 + 64);
    EXPECT_EQ(b.levels[1][0], 64);
    EXPECT_EQ(b.levels[2], nullptr);
    EXPECT_EQ(b.clut, format0.data() + 0x80 + 0x60 + 80);
    EXPECT_EQ(b.clut[0], 0xC0);
    EXPECT_FALSE(b.clut16);
    EXPECT_TRUE(b.clut_csm1);
    EXPECT_EQ(b.clut_colors, 256u);
    EXPECT_EQ(b.tex0, 0xAAAAull);
    EXPECT_EQ(b.tex1, 0xBBBBull);
    EXPECT_EQ(b.total_size, 0x60u + 80 + 1024);

    const std::vector<uint8_t> format1 = MakeFile(1, {picture_c});
    std::optional<std::vector<tim2::Picture>> aligned = tim2::Parse(format1);
    ASSERT_TRUE(aligned.has_value());
    ASSERT_EQ(aligned->size(), 1u);
    const tim2::Picture &c = (*aligned)[0];
    EXPECT_EQ(c.psm, tim2::Psm::T4);
    EXPECT_EQ(c.width, 16u);
    EXPECT_EQ(c.height, 16u);
    EXPECT_EQ(c.levels[0], format1.data() + 0x80 + 0x30);
    EXPECT_EQ(c.clut, format1.data() + 0x80 + 0xB0);
    EXPECT_TRUE(c.clut16);
    EXPECT_TRUE(c.clut_csm1);
    EXPECT_EQ(c.clut_colors, 16u);
    EXPECT_EQ(c.total_size, 0x30u + 128 + 32);

    const std::span<const uint8_t> whole(format0);
    EXPECT_FALSE(tim2::Parse(whole.first(whole.size() - 1)).has_value());
    EXPECT_FALSE(tim2::Parse(whole.first(8)).has_value());
    const std::span<const uint8_t> picture_span(picture_b);
    EXPECT_FALSE(tim2::ParsePicture(picture_span.first(0x2F)).has_value());
    EXPECT_FALSE(tim2::ParsePicture(picture_span.first(0x60 + 64 + 15)).has_value());
    EXPECT_FALSE(tim2::ParsePicture(picture_span.first(picture_span.size() - 1)).has_value());

    std::vector<uint8_t> bad_magic = format0;
    bad_magic[3] = '3';
    EXPECT_FALSE(tim2::Parse(bad_magic).has_value());

    const std::vector<uint8_t> no_type = BuildPicture({.image_type = 0, .width = 4, .height = 4, .level_sizes = {64}, .pixels = pixels_a});
    EXPECT_FALSE(tim2::ParsePicture(no_type).has_value());
}

TEST(GfxTim2, DecodeRejectsEmptySource) {
    const std::vector<uint8_t> texels(16, 0);
    tim2::Source               source;
    source.width = 4;
    source.height = 4;
    EXPECT_FALSE(tim2::Decode(source).has_value());
    source.levels[0] = texels.data();
    source.width = 0;
    EXPECT_FALSE(tim2::Decode(source).has_value());
}

TEST(GfxTim2, UploadRgbaAndIndexed) {
    dc::test::GfxFixture fixture;

    std::vector<uint8_t> gs(4 * 4 * 4);
    for (size_t y = 0; y < 4; y++) {
        for (size_t x = 0; x < 4; x++) {
            uint8_t *p = &gs[(y * 4 + x) * 4];
            p[0] = static_cast<uint8_t>(x * 40);
            p[1] = static_cast<uint8_t>(y * 40);
            p[2] = 0x10;
            p[3] = 0x80;
        }
    }
    tim2::Source source;
    source.psm = tim2::Psm::Ct32;
    source.width = 4;
    source.height = 4;
    source.levels[0] = gs.data();
    std::optional<tim2::Image> image = tim2::Decode(source);
    ASSERT_TRUE(image.has_value());

    tim2::Uploaded uploaded = tim2::Upload(*image, 8, 8);
    ASSERT_NE(uploaded.texture, gfx::kNullTexture);
    EXPECT_EQ(uploaded.palette, gfx::kNullTexture);
    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;
    ASSERT_TRUE(gfx::ReadbackTexture(uploaded.texture, pixels, width, height));
    ASSERT_EQ(width, 8u);
    ASSERT_EQ(height, 8u);
    ASSERT_EQ(pixels.size(), 8u * 8 * 4);
    for (size_t y = 0; y < 8; y++) {
        for (size_t x = 0; x < 8; x++) {
            const uint8_t *p = &pixels[(y * 8 + x) * 4];
            const std::array<uint8_t, 4> actual = {p[0], p[1], p[2], p[3]};
            const std::array<uint8_t, 4> expected =
                x < 4 && y < 4 ? Texel(*image, 0, x, y) : std::array<uint8_t, 4>{0, 0, 0, 0};
            EXPECT_EQ(actual, expected) << "texel " << x << "," << y;
        }
    }
    gfx::DestroyTexture(uploaded.texture);

    std::vector<uint8_t> indices(8 * 8);
    for (size_t i = 0; i < indices.size(); i++) {
        indices[i] = static_cast<uint8_t>(i * 3);
    }
    std::vector<uint8_t> clut(256 * 4);
    for (size_t p = 0; p < 256; p++) {
        clut[p * 4] = static_cast<uint8_t>(p);
        clut[p * 4 + 1] = static_cast<uint8_t>(255 - p);
        clut[p * 4 + 2] = 0x10;
        clut[p * 4 + 3] = 0x80;
    }
    tim2::Source indexed;
    indexed.psm = tim2::Psm::T8;
    indexed.width = 8;
    indexed.height = 8;
    indexed.levels[0] = indices.data();
    indexed.clut = clut.data();
    indexed.csm1 = false;
    std::optional<tim2::Image> index_image = tim2::Decode(indexed);
    ASSERT_TRUE(index_image.has_value());

    uploaded = tim2::Upload(*index_image);
    ASSERT_NE(uploaded.texture, gfx::kNullTexture);
    ASSERT_NE(uploaded.palette, gfx::kNullTexture);
    ASSERT_TRUE(gfx::ReadbackTexture(uploaded.texture, pixels, width, height));
    EXPECT_EQ(width, 8u);
    EXPECT_EQ(height, 8u);
    EXPECT_EQ(pixels, index_image->levels[0].pixels);

    ASSERT_TRUE(gfx::ReadbackTexture(uploaded.palette, pixels, width, height));
    EXPECT_EQ(width, 256u);
    EXPECT_EQ(height, 1u);
    ASSERT_EQ(pixels.size(), 256u * 4);
    for (size_t p = 0; p < 256; p++) {
        const uint32_t packed = static_cast<uint32_t>(pixels[p * 4]) | static_cast<uint32_t>(pixels[p * 4 + 1]) << 8 |
                                static_cast<uint32_t>(pixels[p * 4 + 2]) << 16 |
                                static_cast<uint32_t>(pixels[p * 4 + 3]) << 24;
        EXPECT_EQ(packed, index_image->palette[p]) << "entry " << p;
    }
    gfx::DestroyTexture(uploaded.palette);
    gfx::DestroyTexture(uploaded.texture);
}
