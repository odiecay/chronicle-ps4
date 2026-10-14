#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace imagefile {

/// Reads a PNG file as 8-bit RGBA, rows top to bottom. False where the file cannot be read or decoded.
bool LoadPng(const std::filesystem::path &file, int &width, int &height, std::vector<uint8_t> &rgba);

} // namespace imagefile
