#include "../../port/src/texture_asset_palette.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>

static std::array<int, 4> Color(uint32_t p) {
    const int a = p >> 24;
    return {int(p & 255) * a / 255, int((p >> 8) & 255) * a / 255,
            int((p >> 16) & 255) * a / 255, a * 2};
}

int main() {
    std::mt19937 random(0x434c5554);
    for (bool four_bit : {true, false}) {
        std::array<uint32_t, 256> palette;
        for (auto &p : palette) p = random();
        palette[0] = 0;
        palette[1] = 0xff000000;
        palette[2] = 0xff0000ff;
        palette[3] = 0xff00ff00;
        std::vector<uint8_t> rgba;
        for (unsigned i = 0; i < 8192; ++i) {
            uint32_t p = i < 256 ? palette[i] : random();
            for (unsigned c = 0; c < 4; ++c) rgba.push_back(uint8_t(p >> (c * 8)));
        }
        const auto original = palette;
        const auto indices = TextureAssetPalette(palette, four_bit).Encode(rgba);
        assert(palette == original);
        assert(indices.size() * 4 == rgba.size());
        for (size_t i = 0; i < indices.size(); ++i) {
            uint32_t p = 0;
            for (unsigned c = 0; c < 4; ++c) p |= uint32_t(rgba[i * 4 + c]) << (c * 8);
            const auto color = Color(p);
            int distance = std::numeric_limits<int>::max();
            unsigned best = 0;
            for (unsigned j = 0; j < (four_bit ? 16u : 256u); ++j) {
                const auto candidate = Color(palette[j]);
                int d = 0;
                for (unsigned c = 0; c < 4; ++c) d += (color[c] - candidate[c]) * (color[c] - candidate[c]);
                if (d < distance) { distance = d; best = j; }
            }
            assert(indices[i] == best);
        }
        // Transparent RGB must not select an opaque black/coloured entry.
        assert(TextureAssetPalette(palette, four_bit).Encode({255, 0, 255, 0})[0] == 0);
        std::printf("%s CLUT: 8192 matches equal exhaustive search; palette unchanged; transparent edge PASS\n",
                    four_bit ? "16-entry" : "256-entry");
    }
    std::array<uint32_t, 256> palette;
    for (auto &p : palette) p = random() | 0xff000000;
    std::vector<uint8_t> rgba(1024 * 1024 * 4);
    for (auto &p : rgba) p = uint8_t(random());
    const auto start = std::chrono::steady_clock::now();
    auto indices = TextureAssetPalette(palette, false).Encode(rgba);
    std::printf("1024x1024: %zu -> %zu bytes, %.1f ms on Windows host\n", rgba.size(), indices.size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
