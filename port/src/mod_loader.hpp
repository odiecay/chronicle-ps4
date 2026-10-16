#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Version 1 loads texture packs. Later enabled entries have higher priority.
// Assets use Chronicle PR #187's area/language/shared texture layout.
struct TextureMod {
    std::string id;
    std::string name;
    std::string version;
    std::filesystem::path assets;
};

struct TextureMods {
    std::vector<TextureMod> enabled; // highest priority first
    std::vector<std::string> messages;
};

// Reads <root>/load-order.json and <root>/<id>/mod.json. Does not modify data,
// save files, configuration, or the mod directory. An absent order enables none.
TextureMods LoadTextureMods(const std::filesystem::path &root);
TextureMods LoadTextureMods(const std::filesystem::path &root, const std::vector<std::string> &enabled);
bool TextureModIdsValid(const std::vector<std::string> &ids);

struct InstalledMod {
    std::string id;
    std::string name;
    std::string version;
    std::string type;
    std::string description;
    std::string problem;
    bool supported = false;
};
std::vector<InstalledMod> DiscoverTextureMods(const std::filesystem::path &root);

// Called once after configuration and save paths are available.
void InitializeTextureMods();
