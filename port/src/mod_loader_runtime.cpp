#include "mod_loader.hpp"

#include <cstdio>

#include "platform/paths.hpp"
#include "texture_assets.hpp"
#include "platform/config.hpp"
#include "mod_menu_state.hpp"
#include "mod_assets.hpp"
#include "mod_character.hpp"
#include "dataread.hpp"
#include "platform/arena.hpp"

namespace {
ModPackages selected;
ModAssetOverlay overlay;
std::vector<std::filesystem::path> fallback_roots;
bool prepared = false;
bool replace_custom_name = false;
bool identity_configured = false;
}

void InitializeMods() {
    const auto directory = PathsSaveRoot().parent_path() / "mods";
    const auto &config = ConfigGet();
    selected = config.mods_configured ? LoadModPackages(directory, config.enabled_mods) : LoadModPackages(directory);
    if (!prepared) fallback_roots = TextureAssetRoots();
    replace_custom_name = config.character_replace_custom_name;
    identity_configured = false;
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
    if (!identity_configured) ConfigureModCharacter({}, false);
    std::vector<std::string> startup;
    for (auto it = mods.enabled.rbegin(); it != mods.enabled.rend(); ++it) startup.push_back(it->id);
    SetStartupModSelection(startup);
    std::vector<std::filesystem::path> ordered;
    for (const auto &mod : mods.enabled) {
        if (std::filesystem::is_directory(mod.assets)) ordered.push_back(mod.assets);
        if (mod.character && !identity_configured) ConfigureModCharacter(mod.character->default_name, replace_custom_name);
        std::fprintf(stderr, "mods: enabled %s (%s), version %s\n", mod.id.c_str(), mod.name.c_str(), mod.version.c_str());
    }
    ordered.insert(ordered.end(), fallback_roots.begin(), fallback_roots.end());
    TextureAssetsSetRoots(ordered);
    identity_configured = true;
    for (const auto &message : mods.messages) {
        std::fprintf(stderr, "mods: %s\n", message.c_str());
    }
    std::fprintf(stderr, "mods: loader v2, %zu packs enabled, %zu binary file overrides\n", mods.enabled.size(), overlay.files.size());
    std::fflush(stderr);
}

bool ModAssetReadValid(const std::filesystem::path &file, const void *buffer, std::uint64_t size) {
    auto expected = overlay.sizes.find(file);
    if (expected == overlay.sizes.end()) return true;
    if (expected->second != size) return false;
    const auto required = (size+2047)/2048*2048;
    if (required <= overlay.original_capacities.at(file)) return true;
    const auto capacity = ArenaWritableBytes(buffer);
    static unsigned reported = 0;
    if (reported < 64 || required > capacity) {
        std::fprintf(stderr, "mods: resized read %s needs %llu bytes, active arena has %zu: %s\n",
            PathsDisplay(file).c_str(), static_cast<unsigned long long>(required), capacity,
            required <= capacity ? "fits allocation" : "REFUSED");
        ++reported;
    }
    return required <= capacity;
}
