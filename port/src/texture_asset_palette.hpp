#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// An HD image can keep the source image's indexed storage. Its additional texels
// are mapped to the original CLUT, so SetClut, palette animation and GS image
// copies still operate on indices. Transparent RGB does not influence a match.
// The small balanced tree avoids searching all 256 colours for every HD texel.
class TextureAssetPalette {
    struct Point {
        std::array<int, 4> color{};
        unsigned index = 0;
    };
    std::array<Point, 256> points_{};
    unsigned count_;

    static std::array<int, 4> Color(const uint8_t *rgba) {
        const int a = rgba[3];
        return {rgba[0] * a / 255, rgba[1] * a / 255, rgba[2] * a / 255, a * 2};
    }
    void Build(unsigned first, unsigned last, unsigned depth) {
        if (first >= last) return;
        const unsigned mid = (first + last) / 2, axis = depth % 4;
        std::nth_element(points_.begin() + first, points_.begin() + mid, points_.begin() + last,
                         [axis](const Point &a, const Point &b) {
                             return a.color[axis] != b.color[axis] ? a.color[axis] < b.color[axis]
                                                                 : a.index < b.index;
                         });
        Build(first, mid, depth + 1);
        Build(mid + 1, last, depth + 1);
    }
    void Find(const std::array<int, 4> &color, unsigned first, unsigned last, unsigned depth,
              int &best_distance, unsigned &best) const {
        if (first >= last) return;
        const unsigned mid = (first + last) / 2, axis = depth % 4;
        const Point &point = points_[mid];
        int distance = 0;
        for (unsigned c = 0; c < 4; ++c) {
            const int delta = color[c] - point.color[c];
            distance += delta * delta;
        }
        if (distance < best_distance || (distance == best_distance && point.index < best)) {
            best_distance = distance;
            best = point.index;
        }
        const int delta = color[axis] - point.color[axis];
        if (delta < 0) {
            Find(color, first, mid, depth + 1, best_distance, best);
            if (delta * delta <= best_distance) Find(color, mid + 1, last, depth + 1, best_distance, best);
        } else {
            Find(color, mid + 1, last, depth + 1, best_distance, best);
            if (delta * delta <= best_distance) Find(color, first, mid, depth + 1, best_distance, best);
        }
    }

public:
    TextureAssetPalette(const std::array<uint32_t, 256> &palette, bool four_bit) : count_(four_bit ? 16 : 256) {
        for (unsigned i = 0; i < count_; ++i) {
            const uint32_t p = palette[i];
            const uint8_t rgba[] = {uint8_t(p), uint8_t(p >> 8), uint8_t(p >> 16), uint8_t(p >> 24)};
            points_[i] = {Color(rgba), i};
        }
        Build(0, count_, 0);
    }
    std::vector<uint8_t> Encode(const std::vector<uint8_t> &rgba) const {
        std::vector<uint8_t> indices(rgba.size() / 4);
        // Repeated texels and flat regions are common in the source art.
        uint32_t previous = 0;
        uint8_t previous_index = 0;
        bool have_previous = false;
        for (size_t i = 0; i < indices.size(); ++i) {
            const uint8_t *pixel = rgba.data() + i * 4;
            const uint32_t packed = uint32_t(pixel[0]) | uint32_t(pixel[1]) << 8 |
                                    uint32_t(pixel[2]) << 16 | uint32_t(pixel[3]) << 24;
            if (have_previous && packed == previous) {
                indices[i] = previous_index;
                continue;
            }
            int best_distance = std::numeric_limits<int>::max();
            unsigned best = 256;
            Find(Color(pixel), 0, count_, 0, best_distance, best);
            indices[i] = static_cast<uint8_t>(best);
            previous = packed;
            previous_index = indices[i];
            have_previous = true;
        }
        return indices;
    }
};
