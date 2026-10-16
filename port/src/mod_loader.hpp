#pragma once

#include <filesystem>
#include <string>
#include <cstdint>
#include <optional>
#include <vector>

struct ModDependency { std::string id, version; }; // empty version accepts any; otherwise exact
struct ModFile {
    std::string game_path;
    std::filesystem::path source;
    std::uint64_t size;
    std::string original_fnv, replacement_fnv;
    std::uint64_t original_size = 0;
};
struct ModCharacter { int slot = 0; std::string default_name; };
// Texture packs use PR #187's layout. Asset/character packs additionally declare
// exact-size file replacements, tied to the original and replacement contents.
struct ModPackage {
    std::string id;
    std::string name;
    std::string version;
    std::filesystem::path assets;
    std::string type = "texture-pack";
    std::vector<ModDependency> requires_mods;
    std::vector<std::string> conflicts;
    std::vector<ModFile> files;
    std::optional<ModCharacter> character;
    bool arena_checked_growth = false;
};

struct ModPackages {
    std::vector<ModPackage> enabled; // highest priority first
    std::vector<std::string> messages;
};

// Reads <root>/load-order.json and <root>/<id>/mod.json. Does not modify data,
// save files, configuration, or the mod directory. An absent order enables none.
ModPackages LoadModPackages(const std::filesystem::path &root);
ModPackages LoadModPackages(const std::filesystem::path &root, const std::vector<std::string> &enabled);
bool ModIdsValid(const std::vector<std::string> &ids);

struct InstalledMod {
    std::string id;
    std::string name;
    std::string version;
    std::string type;
    std::string description;
    std::string problem;
    bool supported = false;
    std::vector<ModDependency> requires_mods;
    std::vector<std::string> conflicts;
};
std::vector<InstalledMod> DiscoverMods(const std::filesystem::path &root);

// Called at startup, or at a drained title-screen reload before assets load.
void InitializeMods();
// Selection/name changes wait for the title boundary, never a live game area.
bool ModSelectionPending();
