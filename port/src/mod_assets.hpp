#pragma once
#include "mod_loader.hpp"
#include <map>
#include <unordered_map>

using GameAssetIndex = std::unordered_map<std::string, std::filesystem::path>;
struct ModAssetOverlay {
    GameAssetIndex files;
    std::map<std::filesystem::path, std::uint64_t> sizes;
    std::map<std::filesystem::path, std::uint64_t> original_capacities;
    std::vector<std::string> messages;
};
// FNV is a content/version fingerprint, not an authenticity/security signature.
std::string ModFileFingerprint(const std::filesystem::path &file);
// Validate every binary replacement before changing the game's index. A bad
// component rejects the selection as a whole, so a character cannot half-load.
bool ValidateModAssets(const ModPackages &mods, const GameAssetIndex &original, ModAssetOverlay &out);
void ApplyModAssetOverrides(GameAssetIndex &index);
bool ModAssetReadValid(const std::filesystem::path &file, const void *buffer, std::uint64_t size);
