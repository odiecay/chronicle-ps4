#include "mod_loader.hpp"
#include "texture_assets.hpp"
#include "platform/paths.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static fs::path save_path;
const fs::path &PathsSaveRoot() { return save_path; }
fs::path PathsExecutable() { return {}; }

void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void File(const fs::path &path, const std::string &text = "") {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}
int main(int argc, char **argv) {
    try {
        Check(argc == 2, "pass a fresh output directory");
        const fs::path root = argv[1];
        Check(!fs::exists(root), "output exists; preserving previous evidence");
        save_path = root / "save";
        for (const auto &id : {"base-pack", "patch-pack"}) {
            File(root / id / "mod.json", std::string("{\"format\":1,\"id\":\"") + id +
                "\",\"name\":\"Test\",\"version\":\"1\",\"game\":\"dark-cloud\",\"type\":\"texture-pack\"}");
            File(root / id / "assets/textures/hero.png");
        }
        File(root / "load-order.json", R"({"format":1,"enabled":["base-pack","patch-pack"]})");
        const auto mods = LoadTextureMods(root);
        std::vector<fs::path> roots;
        for (const auto &m : mods.enabled) roots.push_back(m.assets);
        TextureAssetsSetRoots(roots);
        Check(TextureAssetPath("d01", "a_usa", "hero", 64, 32) == root / "patch-pack/assets/textures/hero.png",
              "load order must reach the actual texture lookup");
        File(root / "patch-pack/assets/a_usa/textures/hero.png");
        Check(TextureAssetPath("d01", "a_usa", "hero", 64, 32) == root / "patch-pack/assets/a_usa/textures/hero.png",
              "language-specific artwork must precede shared artwork");
        File(root / "patch-pack/assets/d01/textures/hero.png");
        File(root / "patch-pack/assets/d01/textures/hero_64x32.png");
        Check(TextureAssetPath("d01", "a_usa", "hero", 64, 32) == root / "patch-pack/assets/d01/textures/hero_64x32.png",
              "area and dimension-specific artwork must take precedence");
        Check(TextureAssetPath("d01", "a_usa", "hero", 128, 64) == root / "patch-pack/assets/d01/textures/hero.png",
              "a size suffix must not override a different size");
        Check(TextureAssetPath("d02", "a_usa", "hero", 64, 32) == root / "patch-pack/assets/a_usa/textures/hero.png",
              "one area's override must not leak to another area");
        Check(TextureAssetPath("d01", "a_usa", "../hero", 64, 32).empty(), "texture names must not escape");
        Check(TextureAssetPath("d01", "a_usa", "C:hero", 64, 32).empty(), "texture names must not name a Windows drive");
        File(root / "load-order.json", R"({"format":1,"enabled":["base-pack"]})");
        roots.clear();
        for (const auto &m : LoadTextureMods(root).enabled) roots.push_back(m.assets);
        TextureAssetsSetRoots(roots);
        Check(TextureAssetPath("d01", "a_usa", "hero", 64, 32) == root / "base-pack/assets/textures/hero.png",
              "disabling a patch must restore the lower-priority mod");
        std::cout << "PASS: manifests drive real area/language/dimension texture lookup and conflict precedence\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
