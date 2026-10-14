#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "gfx/gfx.hpp"
#include "gfx/tim2.hpp"

namespace gfx::tim2 {

namespace {

constexpr std::array<uint8_t, 4> kMagic = {'T', 'I', 'M', '2'};
constexpr size_t                 kFileHeaderSize = 0x10;
constexpr size_t                 kAlignedFirstPicture = 0x80;
constexpr size_t                 kPictureHeaderSize = 0x30;
constexpr size_t                 kMipHeaderSize = 0x60;
constexpr size_t                 kLevelSizesOffset = 0x40;

uint64_t LoadLe(const uint8_t *p, size_t bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; i++) {
        value |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return value;
}

uint32_t MipExtent(uint32_t extent, uint32_t level) { return std::max(1u, extent >> level); }

bool IsIndexed(Psm psm) { return psm == Psm::T8 || psm == Psm::T4; }

uint32_t PackRgba(const std::array<uint8_t, 4> &rgba) {
    return static_cast<uint32_t>(rgba[0]) | static_cast<uint32_t>(rgba[1]) << 8 |
           static_cast<uint32_t>(rgba[2]) << 16 | static_cast<uint32_t>(rgba[3]) << 24;
}

uint8_t PsAlpha(uint8_t alpha) {
    return static_cast<uint8_t>(std::min(static_cast<uint32_t>(alpha) * 2u, 255u));
}

std::array<uint8_t, 4> Ct32Texel(const uint8_t *p) { return {p[0], p[1], p[2], PsAlpha(p[3])}; }

// The GS keeps 5 bits per channel and does not replicate them into the low bits.
std::array<uint8_t, 4> Ct16Texel(uint16_t value, const Texa &texa) {
    const auto r = static_cast<uint8_t>((value & 0x1Fu) << 3);
    const auto g = static_cast<uint8_t>(((value >> 5) & 0x1Fu) << 3);
    const auto b = static_cast<uint8_t>(((value >> 10) & 0x1Fu) << 3);
    uint8_t    alpha = texa.ta0;
    if (value & 0x8000u) {
        alpha = texa.ta1;
    } else if (texa.aem && r == 0 && g == 0 && b == 0) {
        alpha = 0;
    }
    return {r, g, b, PsAlpha(alpha)};
}

std::array<uint8_t, 4> ClutEntry(const uint8_t *entry, bool clut16, const Texa &texa) {
    return clut16 ? Ct16Texel(static_cast<uint16_t>(LoadLe(entry, 2)), texa) : Ct32Texel(entry);
}

void WriteTexel(uint8_t *dst, size_t texel, const std::array<uint8_t, 4> &rgba) {
    std::copy(rgba.begin(), rgba.end(), dst + texel * 4);
}

// The block layout only addresses whole 16x16 blocks; partial ones stay linear.
bool WholeBlocks(uint32_t width, uint32_t height) { return width % 16 == 0 && height % 16 == 0; }

uint32_t SwizzledIndex(uint32_t x, uint32_t y, uint32_t width) {
    const uint32_t block_location = (y & ~0xFu) * width + (x & ~0xFu) * 2;
    const uint32_t swap_selector = (((y + 2) >> 2) & 1) * 4;
    const uint32_t pos_y = (((y & ~3u) >> 1) + (y & 1)) & 7;
    const uint32_t column_location = pos_y * width * 2 + ((x + swap_selector) & 7) * 4;
    const uint32_t byte_num = ((y >> 1) & 1) + ((x >> 2) & 2);
    return block_location + column_location + byte_num;
}

} // namespace

size_t LevelBytes(Psm psm, uint32_t width, uint32_t height) {
    const auto texels = static_cast<size_t>(width) * height;
    switch (psm) {
        case Psm::Ct32:
            return texels * 4;
        case Psm::Ct24:
            return texels * 3;
        case Psm::Ct16:
            return texels * 2;
        case Psm::T8:
            return texels;
        case Psm::T4:
            return (texels + 1) / 2;
    }
    return 0;
}

uint32_t ClutCsm1(uint32_t index) {
    return (index & ~0x18u) | ((index & 0x08u) << 1) | ((index & 0x10u) >> 1);
}

void Swizzle8(const uint8_t *src, uint8_t *dst, uint32_t width, uint32_t height) {
    if (!WholeBlocks(width, height)) {
        std::copy_n(src, static_cast<size_t>(width) * height, dst);
        return;
    }
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            dst[SwizzledIndex(x, y, width)] = src[static_cast<size_t>(y) * width + x];
        }
    }
}

void Unswizzle8(const uint8_t *src, uint8_t *dst, uint32_t width, uint32_t height) {
    if (!WholeBlocks(width, height)) {
        std::copy_n(src, static_cast<size_t>(width) * height, dst);
        return;
    }
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            dst[static_cast<size_t>(y) * width + x] = src[SwizzledIndex(x, y, width)];
        }
    }
}

std::array<uint32_t, 256> DecodePalette(const Source &source) {
    std::array<uint32_t, 256> palette{};
    if (source.clut == nullptr || !IsIndexed(source.psm)) {
        return palette;
    }
    const bool     t8 = source.psm == Psm::T8;
    const uint32_t count = t8 ? 256 : 16;
    const size_t   entry_bytes = source.clut16 ? 2 : 4;
    for (uint32_t p = 0; p < count; p++) {
        const uint32_t slot = t8 && source.csm1 ? ClutCsm1(p) : p;
        palette[slot] = PackRgba(ClutEntry(source.clut + p * entry_bytes, source.clut16, source.texa));
    }
    return palette;
}

std::optional<Image> Decode(const Source &source) {
    if (source.width == 0 || source.height == 0 || source.levels[0] == nullptr) {
        return std::nullopt;
    }
    Image image;
    image.format = IsIndexed(source.psm) ? TextureFormat::Index8 : TextureFormat::Rgba8;
    image.has_alpha = source.psm != Psm::Ct24;
    const size_t texel_bytes = image.format == TextureFormat::Index8 ? 1 : 4;

    for (uint32_t i = 0; i < kMaxLevels && source.levels[i] != nullptr; i++) {
        Level level;
        level.width = MipExtent(source.width, i);
        level.height = MipExtent(source.height, i);
        const auto    texels = static_cast<size_t>(level.width) * level.height;
        const uint8_t *src = source.levels[i];
        level.pixels.resize(texels * texel_bytes);
        uint8_t *dst = level.pixels.data();

        switch (source.psm) {
            case Psm::Ct32:
                for (size_t t = 0; t < texels; t++) {
                    WriteTexel(dst, t, Ct32Texel(src + t * 4));
                }
                break;
            case Psm::Ct24:
                for (size_t t = 0; t < texels; t++) {
                    WriteTexel(dst, t, {src[t * 3], src[t * 3 + 1], src[t * 3 + 2], 0xFF});
                }
                break;
            case Psm::Ct16:
                for (size_t t = 0; t < texels; t++) {
                    WriteTexel(dst, t, Ct16Texel(static_cast<uint16_t>(LoadLe(src + t * 2, 2)), source.texa));
                }
                break;
            case Psm::T8:
                if (source.swizzled) {
                    Unswizzle8(src, dst, level.width, level.height);
                } else {
                    std::copy_n(src, texels, dst);
                }
                break;
            case Psm::T4:
                for (size_t t = 0; t < texels; t++) {
                    const uint8_t byte = src[t / 2];
                    dst[t] = static_cast<uint8_t>((t & 1) ? byte >> 4 : byte & 0x0F);
                }
                break;
        }
        image.levels.push_back(std::move(level));
    }
    image.palette = DecodePalette(source);
    return image;
}

namespace {

bool ParseLevels(std::span<const uint8_t> bytes, Picture &picture, size_t header_size, uint32_t mipmap_count) {
    const uint8_t *base = bytes.data();
    const bool     mipmapped = mipmap_count > 1;
    picture.level_count = std::min(std::max(mipmap_count, 1u), kMaxLevels);
    size_t offset = header_size;
    for (uint32_t i = 0; i < picture.level_count; i++) {
        const size_t needed = LevelBytes(picture.psm, MipExtent(picture.width, i), MipExtent(picture.height, i));
        if (offset > bytes.size() || needed > bytes.size() - offset) {
            return false;
        }
        picture.levels[i] = base + offset;
        if (mipmapped) {
            offset += LoadLe(base + kLevelSizesOffset + 4 * i, 4);
        }
    }
    return true;
}

} // namespace

std::optional<Picture> ParsePicture(std::span<const uint8_t> bytes) {
    if (bytes.size() < kPictureHeaderSize) {
        return std::nullopt;
    }
    const uint8_t *base = bytes.data();
    Picture        picture;
    picture.total_size = static_cast<uint32_t>(LoadLe(base, 4));
    const auto clut_size = static_cast<uint32_t>(LoadLe(base + 4, 4));
    const auto image_size = static_cast<uint32_t>(LoadLe(base + 8, 4));
    const auto header_size = static_cast<size_t>(LoadLe(base + 0xC, 2));
    picture.clut_colors = static_cast<uint32_t>(LoadLe(base + 0xE, 2));
    const uint32_t mipmap_count = base[0x11];
    const uint8_t  clut_type = base[0x12];
    const uint8_t  image_type = base[0x13];
    picture.width = static_cast<uint32_t>(LoadLe(base + 0x14, 2));
    picture.height = static_cast<uint32_t>(LoadLe(base + 0x16, 2));
    picture.tex0 = LoadLe(base + 0x18, 8);
    picture.tex1 = LoadLe(base + 0x20, 8);

    switch (image_type) {
        case 1:
            picture.psm = Psm::Ct16;
            break;
        case 2:
            picture.psm = Psm::Ct24;
            break;
        case 3:
            picture.psm = Psm::Ct32;
            break;
        case 4:
            picture.psm = Psm::T4;
            break;
        case 5:
            picture.psm = Psm::T8;
            break;
        default:
            return std::nullopt;
    }
    if (picture.width == 0 || picture.height == 0 || picture.total_size > bytes.size()) {
        return std::nullopt;
    }
    const bool mipmapped = mipmap_count > 1;
    if (bytes.size() < (mipmapped ? kMipHeaderSize : kPictureHeaderSize)) {
        return std::nullopt;
    }
    if (!ParseLevels(bytes, picture, header_size, mipmap_count)) {
        return std::nullopt;
    }

    picture.clut16 = (clut_type & 0x3F) == 1;
    picture.clut_csm1 = (clut_type & 0xC0) == 0;
    if (IsIndexed(picture.psm) && clut_size > 0) {
        const size_t entries = picture.psm == Psm::T8 ? 256 : 16;
        const size_t clut_bytes = entries * (picture.clut16 ? 2 : 4);
        const size_t clut_offset = header_size + image_size;
        if (clut_offset > bytes.size() || clut_bytes > bytes.size() - clut_offset) {
            return std::nullopt;
        }
        picture.clut = base + clut_offset;
    }
    return picture;
}

std::optional<std::vector<Picture>> Parse(std::span<const uint8_t> file) {
    if (file.size() < kFileHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), file.begin())) {
        return std::nullopt;
    }
    size_t first = 0;
    switch (file[5]) {
        case 0:
            first = kFileHeaderSize;
            break;
        case 1:
            first = kAlignedFirstPicture;
            break;
        default:
            return std::nullopt;
    }
    const auto count = static_cast<uint32_t>(LoadLe(file.data() + 6, 2));
    std::vector<Picture> pictures;
    size_t               offset = first;
    for (uint32_t i = 0; i < count; i++) {
        if (offset > file.size()) {
            return std::nullopt;
        }
        std::optional<Picture> picture = ParsePicture(file.subspan(offset));
        if (!picture) {
            return std::nullopt;
        }
        offset += picture->total_size;
        pictures.push_back(*picture);
    }
    return pictures;
}

Uploaded Upload(const Image &image, uint32_t canvas_width, uint32_t canvas_height) {
    if (image.levels.empty()) {
        return {};
    }
    const Level   &base = image.levels.front();
    const uint32_t width = std::max(base.width, canvas_width);
    const uint32_t height = std::max(base.height, canvas_height);
    const auto     max_mip_levels = static_cast<uint32_t>(std::bit_width(std::max(width, height)));
    const uint32_t mip_levels = std::min(static_cast<uint32_t>(image.levels.size()), max_mip_levels);

    const TextureHandle texture = CreateTexture({width, height, image.format, mip_levels, image.has_alpha});
    if (texture == kNullTexture) {
        return {};
    }
    const size_t texel_bytes = image.format == TextureFormat::Index8 ? 1 : 4;
    for (uint32_t i = 0; i < mip_levels; i++) {
        const Level &level = image.levels[i];
        if (level.pixels.size() != static_cast<size_t>(level.width) * level.height * texel_bytes ||
            !UpdateTexture(texture, i, 0, 0, level.width, level.height, level.pixels.data())) {
            DestroyTexture(texture);
            return {};
        }
    }

    TextureHandle palette = kNullTexture;
    if (image.format == TextureFormat::Index8) {
        palette = CreatePalette();
        if (palette == kNullTexture || !UploadPalette(palette, image)) {
            if (palette != kNullTexture) {
                DestroyTexture(palette);
            }
            DestroyTexture(texture);
            return {};
        }
    }
    return {texture, palette};
}

bool UploadPalette(TextureHandle palette, const Image &image) {
    return UpdatePalette(palette, image.palette.data(), 0, 256);
}

} // namespace gfx::tim2
