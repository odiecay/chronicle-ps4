#include "platform/imagefile.hpp"

#include <fstream>
#include <iterator>

// The decoder itself is compiled once, in glyphs.cpp.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace imagefile {

bool LoadPng(const std::filesystem::path &file, int &width, int &height, std::vector<uint8_t> &rgba) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    int                        channels = 0;
    unsigned char             *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                                              &channels, 4);
    if (pixels == nullptr) {
        return false;
    }
    rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

} // namespace imagefile
