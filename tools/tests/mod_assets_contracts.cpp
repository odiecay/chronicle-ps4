#include "mod_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
using Json = nlohmann::json;
void Check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
void File(const fs::path &p, std::string_view bytes) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << bytes; }
int main(int argc, char **argv) {
 try {
    Check(argc == 2 && !fs::exists(argv[1]), "use a fresh fixture directory");
    const fs::path root = argv[1];
    File(root/"original/hero.chr", "original"); File(root/"mods/hero/files/hero.chr", "replaced");
    Json manifest{{"format",2},{"id","hero"},{"name","Hero"},{"version","1"},{"game","dark-cloud"},{"type","character-pack"},
        {"character",{{"slot",0},{"default_name","Tora"}}},
        {"files",Json::array({{{"path","chara/hero.chr"},{"source","files/hero.chr"},{"size",8},
            {"original_fnv1a64",ModFileFingerprint(root/"original/hero.chr")},
            {"replacement_fnv1a64",ModFileFingerprint(root/"mods/hero/files/hero.chr")}}})}};
    auto write = [&] { File(root/"mods/hero/mod.json", manifest.dump()); };
    write();
    GameAssetIndex original{{"chara/hero.chr",root/"original/hero.chr"}};
    auto mods = LoadModPackages(root/"mods", {"hero"});
    Check(mods.enabled.size() == 1 && mods.enabled[0].character->default_name == "Tora", "validated character metadata");
    ModAssetOverlay overlay;
    Check(ValidateModAssets(mods, original, overlay) && overlay.files.size() == 1, "actual content match enables overlay");
    Check(original.at("chara/hero.chr") == root/"original/hero.chr", "validation never changes base data");
    File(root/"original/hero.chr", "wrongrev");
    Check(!ValidateModAssets(mods, original, overlay) && overlay.files.empty(), "same-size wrong base revision must reject atomically");
    File(root/"original/hero.chr", "original"); File(root/"mods/hero/files/hero.chr", "tampered");
    Check(!ValidateModAssets(mods, original, overlay), "changed replacement content must reject");
    File(root/"mods/hero/files/hero.chr", "replaced-more");
    Check(LoadModPackages(root/"mods", {"hero"}).enabled.empty(), "larger binary must not enter fixed guest buffers");
    File(root/"mods/hero/files/hero.chr", "replaced");
    manifest["files"][0]["source"] = "../escape.chr"; write();
    Check(LoadModPackages(root/"mods", {"hero"}).enabled.empty(), "replacement paths must stay inside pack");
    manifest["files"][0]["source"] = "files/hero.chr";
    manifest["requires"] = Json::array({{{"id","base"},{"version","2"}}}); write();
    Check(LoadModPackages(root/"mods", {"hero"}).enabled.empty(), "missing declared dependency must reject selection");
    fs::create_directories(root/"mods/base/assets");
    Json base{{"format",1},{"id","base"},{"name","Base"},{"version","1"},{"game","dark-cloud"},{"type","texture-pack"}};
    File(root/"mods/base/mod.json",base.dump());
    Check(LoadModPackages(root/"mods", {"base","hero"}).enabled.empty(), "dependency version mismatch must reject");
    base["version"]="2"; File(root/"mods/base/mod.json",base.dump());
    Check(LoadModPackages(root/"mods", {"base","hero"}).enabled.size()==2, "matching exact dependency version accepted");
    base["conflicts"]=Json::array({"hero"}); File(root/"mods/base/mod.json",base.dump());
    Check(LoadModPackages(root/"mods", {"base","hero"}).enabled.empty(), "conflict declared in either direction rejects");
    Check(DiscoverMods(root/"mods").size()==2, "discovery keeps dependency-bound packs visible");
    File(root/"hash-vector", "hello");
    Check(ModFileFingerprint(root/"hash-vector")=="a430d84680aabd0b", "known FNV-1a vector");
    std::cout << "PASS: original/replacement identity, bounds, dependencies, conflicts, atomic overlays\n";
    return 0;
 } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
