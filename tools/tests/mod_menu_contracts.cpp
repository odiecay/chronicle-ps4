#include "options/rows.hpp"
#include "mod_loader.hpp"
#include "mod_menu_state.hpp"
#include "platform/paths.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static fs::path save;
static int browser_opens;
// Window/UI drawing is exercised by the complete target build and hardware;
// here this adapter records dispatch from the actual action row.
namespace options { void OpenWeaponBrowser() { ++browser_opens; } }
const fs::path &PathsSaveRoot() { return save; }
// This suite exercises configuration persistence, not a desktop mode change.
void DisplayChanged(const HostSettings &, const HostSettings &) {}
void Check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
void File(const fs::path &p, const std::string &text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
void Pack(const fs::path &root, const std::string &id, const std::string &type = "texture-pack") {
    fs::create_directories(root / id / "assets/textures");
    File(root / id / "mod.json", "{\"format\":1,\"id\":\"" + id + "\",\"name\":\"" + id +
         "\",\"version\":\"1\",\"game\":\"dark-cloud\",\"type\":\"" + type + "\"}");
}
const options::Row &Row(const std::string &id) {
    for (const auto &r : options::ModRows()) if (r.key == std::string("mods.pack.") + id) return r;
    throw std::runtime_error("mod absent from actual menu rows");
}
const options::Row &Native(const std::string &key) {
    for (const auto &r : options::ModRows()) if (r.key == key) return r;
    throw std::runtime_error("native module absent from actual menu rows");
}
int main(int argc, char **argv) {
    try {
        Check(argc == 2, "pass a fresh output directory");
        const fs::path root = argv[1];
        Check(!fs::exists(root), "output exists; previous evidence preserved");
        save = root / "save";
        fs::create_directories(save);
        const auto mods = root / "mods";
        options::PrepareModMenu();
        Check(Native("mods.empty").count(Config{}) == 1, "empty external catalog must explain pack installation");
        Native("mods.weapon_browser").activate();
        Check(browser_opens == 1, "weapon browser row must dispatch its action");
        Config native;
        Check(!native.town_autosave && !native.swap_keeps_lock_on, "gameplay modules must be opt-in");
        Native("mods.town_autosave").set(native, 1);
        Native("game.swap_keeps_lock_on").set(native, 1);
        Check(ConfigParse(ConfigSerialize(native)) == native && native.town_autosave && native.swap_keeps_lock_on,
              "native module settings must persist through actual serializer");
        Pack(mods, "base"); Pack(mods, "patch"); Pack(mods, "character", "character-pack");
        SetStartupModSelection({"base"});
        Config c;
        c.options.soft_focus = false;
        options::PrepareModMenu();
        Check(options::ModRows().size() == 9, "native modules coexist with installed and unavailable packs");
        Check(Row("base").get(c) == 1 && Row("base").text(c) == "On", "legacy active packs must appear enabled");
        const auto opened = c;
        Row("patch").set(c, 1);
        Check(c.mods_configured && c.enabled_mods == std::vector<std::string>({"base", "patch"}),
              "menu enable must preserve legacy selection and append priority");
        Check(Row("patch").text(c) == "On *" && !ModActive("patch"),
              "menu must mark pending changes without hot-swapping loaded game resources");
        Check(ConfigChange(c), "actual ConfigChange must save the selected mods");
        Check(ConfigLoad() && ConfigGet().enabled_mods == c.enabled_mods && !ConfigGet().options.soft_focus,
              "actual config reload must restore selection and retain graphics preferences");
        Check(ConfigParse(ConfigSerialize(c)) == c, "config round-trip must retain all mod state");
        Check(ConfigAppliesOnRestart("mods.enabled"), "mod selection must declare restart semantics");
        auto next = LoadModPackages(mods, ConfigGet().enabled_mods);
        Check(next.enabled.size() == 2 && next.enabled[0].id == "patch", "saved menu selection must feed the loader");
        c = opened;
        Check(!ModRequested(c, "patch") && !ModChangePending(c, "base"), "undo snapshot must restore original selection");
        Row("base").set(c, 0);
        Check(Row("base").text(c) == "Off *" && ModActive("base"), "disable applies after restart, not immediately");
        Row("base").set(c, 1);
        Check(Row("base").text(c) == "On", "reverting a change must clear its pending marker");
        const auto before = c;
        Row("character").set(c, 1);
        Check(Row("character").count(c) == 1 && Row("character").text(c) == "Unavailable" && c == before,
              "unsupported character packs must never pretend to enable");
        Row("base").restore(c, Config{});
        Check(!ModRequested(c, "base"), "reset page must disable supported mods");
        Check(!ConfigParse(R"({"mods":{"enabled":["../escape"]}})").mods_configured, "invalid saved IDs rejected");
        Check(!ConfigParse(R"({"mods":{"enabled":["base","base"]}})").mods_configured, "duplicate saved IDs rejected");
        Pack(mods, "new-pack");
        options::PrepareModMenu();
        Check(options::ModRows().size() == 10, "reopening menu discovers newly installed mods");
        std::cout << "PASS: real Mods rows, enable/disable, persistence, undo, restart state and unsupported packs\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
