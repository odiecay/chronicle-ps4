#include "mod_assets.hpp"
#include <array>
#include <cstdio>
#include <fstream>

std::string ModFileFingerprint(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    std::uint64_t hash = 14695981039346656037ull;
    std::array<char, 65536> bytes;
    while (input) {
        input.read(bytes.data(), bytes.size());
        for (std::streamsize i = 0; i < input.gcount(); ++i) { hash ^= static_cast<unsigned char>(bytes[i]); hash *= 1099511628211ull; }
    }
    if (!input.eof()) return {};
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}
bool ValidateModAssets(const ModPackages &mods, const GameAssetIndex &original, ModAssetOverlay &out) {
    ModAssetOverlay candidate;
    // Highest priority first, exactly as texture lookup. All lower-priority
    // components are still validated, including a file hidden by another pack.
    for (const auto &mod : mods.enabled) for (const auto &file : mod.files) {
        const auto base = original.find(file.game_path);
        std::error_code ec;
        const auto source_size = std::filesystem::file_size(file.source, ec);
        if (base == original.end() || ec || source_size != file.size ||
            std::filesystem::file_size(base->second, ec) != (file.original_size ? file.original_size : file.size) || ec ||
            ModFileFingerprint(base->second) != file.original_fnv || ModFileFingerprint(file.source) != file.replacement_fnv) {
            out = {};
            out.messages.push_back(mod.id + ": original/replacement size or content mismatch: " + file.game_path);
            return false;
        }
        auto [it, inserted] = candidate.files.emplace(file.game_path, file.source);
        if (!inserted && candidate.messages.size() < 32)
            candidate.messages.push_back("file conflict " + file.game_path + "; higher-priority pack retained");
        candidate.sizes.emplace(file.source, file.size);
        const auto original_size = file.original_size ? file.original_size : file.size;
        candidate.original_capacities.emplace(file.source, (original_size+2047)/2048*2048);
    }
    out = std::move(candidate);
    return true;
}
