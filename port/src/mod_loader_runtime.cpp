#include "mod_loader.hpp"

#include <cstdio>

#include "platform/paths.hpp"
#include "texture_assets.hpp"
#include "platform/config.hpp"
#include "mod_menu_state.hpp"
#include "mod_assets.hpp"
#include "mod_character.hpp"
#include "dataread.hpp"

namespace {
ModPackages selected;
ModAssetOverlay overlay;
std::vector<std::filesystem::path> fallback_roots;
bool prepared = false;
bool replace_custom_name = false;
}

void InitializeMods() {
    const auto directory = PathsSaveRoot().parent_path() / "mods";
    const auto &config = ConfigGet();
    selected = config.mods_configured ? LoadModPackages(directory, config.enabled_mods) : LoadModPackages(directory);
    fallback_roots = TextureAssetRoots();
    replace_custom_name = config.character_replace_custom_name;
    prepared = true;
    // Establish the original, normalized game index, validate the whole package
    // selection against it, then activate before any model/name is initialized.
    InitCDFile();
}

void ApplyModAssetOverrides(GameAssetIndex &index) {
    if (!prepared) return;
    auto mods = selected; // frozen at startup; config changes still require restart
    if (!ValidateModAssets(mods, index, overlay)) {
        mods.messages.insert(mods.messages.end(), overlay.messages.begin(), overlay.messages.end());
        mods.enabled.clear();
    } else {
        for (const auto &[path, source] : overlay.files) index[path] = source;
        mods.messages.insert(mods.messages.end(), overlay.messages.begin(), overlay.messages.end());
    }
    ConfigureModCharacter({}, false);
    std::vector<std::string> startup;
    for (auto it = mods.enabled.rbegin(); it != mods.enabled.rend(); ++it) startup.push_back(it->id);
    SetStartupModSelection(startup);
    std::vector<std::filesystem::path> ordered;
    for (const auto &mod : mods.enabled) {
        if (std::filesystem::is_directory(mod.assets)) ordered.push_back(mod.assets);
        if (mod.character) ConfigureModCharacter(mod.character->default_name, replace_custom_name);
        std::fprintf(stderr, "mods: enabled %s (%s), version %s\n", mod.id.c_str(), mod.name.c_str(), mod.version.c_str());
    }
    ordered.insert(ordered.end(), fallback_roots.begin(), fallback_roots.end());
    TextureAssetsSetRoots(ordered);
    for (const auto &message : mods.messages) {
        std::fprintf(stderr, "mods: %s\n", message.c_str());
    }
    std::fprintf(stderr, "mods: loader v2, %zu packs enabled, %zu binary file overrides\n", mods.enabled.size(), overlay.files.size());
    std::fflush(stderr);
}

bool ModAssetReadSizeValid(const std::filesystem::path &file, std::uint64_t size) {
    auto expected = overlay.sizes.find(file);
    return expected == overlay.sizes.end() || expected->second == size;
}
