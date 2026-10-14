#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

#include "../gfx/displaylist.hpp"
#include "context.hpp"

namespace gfx {

namespace detail {

namespace {

uint32_t Crc32(const uint8_t *data, size_t size, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> entries{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; bit++) {
                value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            }
            entries[i] = value;
        }
        return entries;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void PutBigEndian(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void PutChunk(std::vector<uint8_t> &out, const char type[4], const std::vector<uint8_t> &data) {
    PutBigEndian(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    PutBigEndian(out, Crc32(out.data() + start, out.size() - start));
}

} // namespace

void ReadImagePixels(const Image &image, std::vector<uint8_t> &pixels) {
    pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 0);
    BindFbo(FramebufferFor(image, nullptr));
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    // Rows come out lowest first; the image's first row is the lowest, so they are already top to bottom.
    gl.ReadPixels(0, 0, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels.data());
}

} // namespace detail

using namespace detail;

void ReadDepth(uint32_t id, float x, float y, float w, float h) {
    if (id >= kDepthQueryCount) {
        Error("ReadDepth: query %u is past %u", id, kDepthQueryCount);
        return;
    }
    if (RecordingCalls()) {
        RecordEntry(ReadDepthEntry{
            id, LogicalRect{x, y, w, h}
        });
        return;
    }
    // ES 2 has no depth readback; DepthResult stays empty and the game treats the pick as a miss.
    DepthQuery &query = g.depth_queries[id];
    query.rect = LogicalRect{x, y, w, h};
    query.queued = true;
    query.valid = false;
}

std::optional<float> DepthResult(uint32_t id) {
    if (id >= kDepthQueryCount || !g.depth_queries[id].valid) {
        return std::nullopt;
    }
    return g.depth_queries[id].result;
}

bool ReadbackFrame(std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height) {
    ApiBarrier();
    if (g.in_frame || RecordingCalls()) {
        Error("ReadbackFrame inside a frame");
        return false;
    }
    Image &image = g.presented_display ? g.display_color : PreviousMainColor();
    width = image.width;
    height = image.height;
    ReadImagePixels(image, rgba);
    return true;
}

namespace detail {

bool SaveLastPresented(const char *path) {
    if (g.last_presented == nullptr || g.last_presented->texture == 0) {
        return false;
    }
    std::vector<uint8_t> rgba;
    ReadImagePixels(*g.last_presented, rgba);
    // The image's first row is the top, the order WritePng takes.
    return WritePng(path, rgba.data(), g.last_presented->width, g.last_presented->height);
}

} // namespace detail

bool ReadbackTexture(TextureHandle handle, std::vector<uint8_t> &pixels, uint32_t &width, uint32_t &height) {
    ApiBarrier();
    if (g.in_frame || RecordingCalls()) {
        Error("ReadbackTexture inside a frame");
        return false;
    }
    Image *image = ColorImageOf(handle);
    if (image == nullptr) {
        Error("ReadbackTexture: no texture %#x", handle);
        return false;
    }
    width = image->width;
    height = image->height;
    if (image->index8) {
        Texture *texture = LookupTexture(handle);
        pixels = texture != nullptr ? texture->shadow : std::vector<uint8_t>();
        return texture != nullptr;
    }
    ReadImagePixels(*image, pixels);
    return true;
}

bool WritePng(const std::filesystem::path &path, const uint8_t *rgba, uint32_t width, uint32_t height) {
    std::vector<uint8_t> raw;
    raw.reserve((static_cast<size_t>(width) * 3 + 1) * height);
    for (uint32_t y = 0; y < height; y++) {
        raw.push_back(0);
        const uint8_t *row = rgba + static_cast<size_t>(y) * width * 4;
        for (uint32_t x = 0; x < width; x++) {
            raw.insert(raw.end(), row + x * 4, row + x * 4 + 3);
        }
    }

    // zlib stream of stored deflate blocks: no compressor, and screenshots are for tests.
    std::vector<uint8_t> zlib = {0x78, 0x01};
    size_t               pos = 0;
    do {
        size_t length = std::min<size_t>(raw.size() - pos, 0xFFFF);
        bool   last = pos + length == raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(length));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        zlib.push_back(static_cast<uint8_t>(~length));
        zlib.push_back(static_cast<uint8_t>(~length >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                    raw.begin() + static_cast<std::ptrdiff_t>(pos + length));
        pos += length;
    } while (pos < raw.size());
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    PutBigEndian(zlib, (b << 16) | a);

    std::vector<uint8_t> header;
    PutBigEndian(header, width);
    PutBigEndian(header, height);
    header.insert(header.end(), {8, 2, 0, 0, 0});

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    PutChunk(png, "IHDR", header);
    PutChunk(png, "IDAT", zlib);
    PutChunk(png, "IEND", {});

    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file &&
           file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
}

} // namespace gfx
