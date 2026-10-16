#include "mod_loader.hpp"

#include <cstdio>

#include "platform/paths.hpp"
#include "texture_assets.hpp"

void InitializeTextureMods() {
    const auto directory = PathsSaveRoot().parent_path() / "mods";
    const auto mods = LoadTextureMods(directory);
    auto roots = TextureAssetRoots();
    std::vector<std::filesystem::path> ordered;
    for (const auto &mod : mods.enabled) {
        ordered.push_back(mod.assets);
        std::fprintf(stderr, "mods: enabled %s (%s), version %s\n", mod.id.c_str(), mod.name.c_str(), mod.version.c_str());
    }
    ordered.insert(ordered.end(), roots.begin(), roots.end());
    TextureAssetsSetRoots(ordered);
    for (const auto &message : mods.messages) {
        std::fprintf(stderr, "mods: %s\n", message.c_str());
    }
    std::fprintf(stderr, "mods: texture loader v1, %zu enabled, root %s\n", mods.enabled.size(), PathsDisplay(directory).c_str());
    std::fflush(stderr);
}
