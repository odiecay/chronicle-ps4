#include "texture_assets.hpp"

// Adapted from Jeffiest/Chronicle, PR #187, head 3c8a816a.
// The named-mod loader supplies additional roots; original lookup layout is retained.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

#include "editloop.hpp"
#include "mainselect.hpp"
#include "localize_texture.hpp"
#include "main.hpp"
#include "platform/imagefile.hpp"
#include "platform/paths.hpp"
#include "texture_port.hpp"
#include "texture_asset_palette.hpp"

extern s32 mode;

namespace fs = std::filesystem;

namespace {

// Pictures already complained about, so a wrong-shaped one says so once.
std::map<std::string, bool> &Reported() {
    static std::map<std::string, bool> reported;
    return reported;
}

// Whether a picture of this shape belongs to a texture of that one (a card is not stretched into a menu sheet).
bool SameShape(int source_width, int source_height, unsigned width, unsigned height) {
    double a = static_cast<double>(source_width) / std::max(1, source_height);
    double b = static_cast<double>(width) / std::max<unsigned>(1, height);
    return std::fabs(a - b) <= 0.02 * b;
}

// Check the PNG's declared allocation before the decoder allocates its pixel array.
// Texture packs are optional, so invalid pictures leave the original texture intact.
bool BoundedPng(const fs::path &path) {
    std::error_code ec;
    const auto bytes = fs::file_size(path, ec);
    if (ec || bytes > 32u * 1024u * 1024u) return false;
    unsigned char header[24]{};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(header), sizeof(header));
    constexpr unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (input.gcount() != sizeof(header) || !std::equal(std::begin(signature), std::end(signature), header) ||
        header[12] != 'I' || header[13] != 'H' || header[14] != 'D' || header[15] != 'R') return false;
    auto u32 = [&](int i) {
        return (uint32_t(header[i]) << 24) | (uint32_t(header[i + 1]) << 16) |
               (uint32_t(header[i + 2]) << 8) | uint32_t(header[i + 3]);
    };
    const uint64_t w = u32(16), h = u32(20);
    return w && h && w <= 8192 && h <= 8192 && w * h * 4 <= 64u * 1024u * 1024u;
}

} // namespace

std::string TextureAssetsArea() {
    switch (mode) {
        case GAME_MODE_DUNGEON:
        case GAME_MODE_LOADER:
            if (main_select_menu_no >= 0 && main_select_menu_no < 7) {
                return "d0" + std::to_string(main_select_menu_no + 1);
            }
            return {};
        case GAME_MODE_EDIT:
            return EditMapName;
        default:
            return {};
    }
}

std::string TextureAssetsLanguage() {
    static const char *const kDirs[7] = {"a_jpn", "a_usa", "a_eng", "a_fre", "a_ger", "a_ita", "a_spa"};
    return LanguageCode >= 0 && LanguageCode < 7 ? kDirs[LanguageCode] : "";
}

bool TextureAssetApply(const char *name, PortDecodedTexture &texture) {
    if (name == nullptr || texture.width == 0 || texture.height == 0 || texture.levels.empty()) {
        return false;
    }
    // A picture is looked for by the texture's own size, so a texture this function already replaced is left alone.
    if (texture.logical_width != 0) {
        return false;
    }
    fs::path path = TextureAssetPath(TextureAssetsArea(), TextureAssetsLanguage(), name, texture.width, texture.height);
    if (path.empty()) {
        return false;
    }
    int                  width = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;
    if (!BoundedPng(path) || !imagefile::LoadPng(path, width, height, rgba)) {
        if (!Reported()[path.string()]) {
            Reported()[path.string()] = true;
            std::fprintf(stderr, "assets: cannot read picture %s\n", PathsDisplay(path).c_str());
        }
        return false;
    }
    if (!SameShape(width, height, texture.width, texture.height)) {
        if (!Reported()[path.string()]) {
            Reported()[path.string()] = true;
            std::fprintf(stderr, "assets: %s is %dx%d, the texture %s is %ux%u: not used\n",
                         PathsDisplay(path).c_str(), width, height, name, texture.width, texture.height);
        }
        return false;
    }

    // The size the picture is kept at: its own if it has the texture's pixels or more (up to the cap), else the
    // texture's.
    int target_width = width;
    int target_height = height;
    if (static_cast<unsigned>(width) < texture.width || static_cast<unsigned>(height) < texture.height) {
        target_width = static_cast<int>(texture.width);
        target_height = static_cast<int>(texture.height);
    } else if (std::max(width, height) > kTextureAssetMaxSide) {
        double k = static_cast<double>(kTextureAssetMaxSide) / std::max(width, height);
        target_width = std::max(1, static_cast<int>(std::lround(width * k)));
        target_height = std::max(1, static_cast<int>(std::lround(height * k)));
    }

    size_t                            level_count = texture.levels.size();
    std::vector<std::vector<uint8_t>> levels;
    bool                              translucent = false;
    // PS4's texture/render-target pool is shared and limited. Expanding a game
    // index texture to RGBA costs four times as much at the same HD resolution
    // and discards the CLUT that the game still updates and copies. Keep its
    // original palette and GS storage contract; only the texel density changes.
#if defined(__PS4__) || defined(DC_TEXTURE_SOURCE_PALETTE)
    const bool keep_palette = texture.format == gfx::TextureFormat::Index8;
#else
    const bool keep_palette = false;
#endif
    for (size_t level = 0; level < level_count; level++) {
        int w = std::max(1, target_width >> level);
        int h = std::max(1, target_height >> level);
        levels.push_back(LocalizeResample(rgba.data(), width, height, w, h));
    }
    for (size_t i = 3; i < levels[0].size(); i += 4) {
        if (levels[0][i] != 255) {
            translucent = true;
            break;
        }
    }
    if (keep_palette) {
        const TextureAssetPalette palette(texture.palette, texture.four_bit);
        for (auto &level : levels) level = palette.Encode(level);
    }
    texture.logical_width = texture.width;
    texture.logical_height = texture.height;
    texture.width = static_cast<unsigned>(target_width);
    texture.height = static_cast<unsigned>(target_height);
    texture.levels = std::move(levels);
    texture.format = keep_palette ? gfx::TextureFormat::Index8 : gfx::TextureFormat::Rgba8;
    texture.has_alpha = translucent || texture.has_alpha;
    if (!keep_palette) texture.four_bit = false;
    static unsigned reported_applies = 0;
    if (reported_applies < 16) {
        ++reported_applies;
        std::fprintf(stderr, "mods: texture %s, logical %ux%u, pixels %ux%u, source %s, storage %s\n", name,
                     texture.logical_width, texture.logical_height, texture.width, texture.height,
                     PathsDisplay(path).c_str(), keep_palette ? "source CLUT / indexed HD" : "RGBA");
    }
    return true;
}
