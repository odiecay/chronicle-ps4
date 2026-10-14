#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "gfx/gfx.hpp"

namespace gfx::tim2 {

enum class Psm : uint8_t { Ct32, Ct24, Ct16, T8, T4 };

struct Texa {
    uint8_t ta0 = 0x80;
    uint8_t ta1 = 0x80;
    bool    aem = true;
};

inline constexpr uint32_t kMaxLevels = 4;

struct Source {
    Psm      psm = Psm::Ct32;
    uint32_t width = 0, height = 0;
    std::array<const uint8_t *, kMaxLevels> levels{}; // level i is max(1, width>>i) x max(1, height>>i); stops at the first null
    const uint8_t *clut = nullptr;                    // T8: 256 entries, T4: 16 entries
    bool           clut16 = false;                    // CLUT entries are PSMCT16 instead of PSMCT32
    bool           csm1 = true;                       // T8 CLUT stored in GS CSM1 order
    bool           swizzled = false;                  // T8 pixels laid out for a PSMCT32 upload at half width and height
    Texa           texa;                              // expands PSMCT16 texels and CLUT entries
};

struct Level {
    uint32_t             width = 0, height = 0;
    std::vector<uint8_t> pixels;
};

struct Image {
    TextureFormat        format = TextureFormat::Rgba8; // Index8 for T8/T4
    bool                 has_alpha = true;              // false for Ct24
    std::vector<Level>   levels;                        // Rgba8: 4 bytes per texel, Index8: 1
    std::array<uint32_t, 256> palette{};                // renderer alpha units, little-endian RGBA (R in the low byte)
};

size_t   LevelBytes(Psm psm, uint32_t width, uint32_t height);
uint32_t ClutCsm1(uint32_t index);
void     Unswizzle8(const uint8_t *src, uint8_t *dst, uint32_t width, uint32_t height);
void     Swizzle8(const uint8_t *src, uint8_t *dst, uint32_t width, uint32_t height);
std::optional<Image> Decode(const Source &source);
std::array<uint32_t, 256> DecodePalette(const Source &source);

struct Picture {
    Psm                                     psm = Psm::Ct32;
    uint32_t                                width = 0, height = 0;
    uint32_t                                level_count = 0;
    std::array<const uint8_t *, kMaxLevels> levels{};
    const uint8_t                          *clut = nullptr;
    bool                                    clut16 = false;
    bool                                    clut_csm1 = true;
    uint32_t                                clut_colors = 0;
    uint64_t                                tex0 = 0, tex1 = 0;
    uint32_t                                total_size = 0;
};

std::optional<Picture>                ParsePicture(std::span<const uint8_t> bytes);
std::optional<std::vector<Picture>>   Parse(std::span<const uint8_t> file);

struct Uploaded {
    TextureHandle texture = kNullTexture;
    TextureHandle palette = kNullTexture;
};

// canvas 0 means the image's own size; a larger canvas leaves the rest zeroed, as GS sampling over 2^TW x 2^TH sees it.
Uploaded Upload(const Image &image, uint32_t canvas_width = 0, uint32_t canvas_height = 0);
bool     UploadPalette(TextureHandle palette, const Image &image);

} // namespace gfx::tim2
