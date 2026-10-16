#include "options/rows.hpp"
#include "mod_loader.hpp"
#include "mod_menu_state.hpp"
#include "platform/paths.hpp"
#include "options/weapon_browser.hpp"
#include <deque>
#include <map>
#include <cstdio>
#include <algorithm>

namespace options {
namespace {
std::vector<Row> rows;
std::deque<std::string> strings;
std::map<std::string, std::string> selection_errors;
const char *Keep(std::string text) { return strings.emplace_back(std::move(text)).c_str(); }
int Two(const Config &) { return 2; }
int One(const Config &) { return 1; }
template<bool Config::*member>
Row Native(const char *key, const char *name, const char *help) {
    return Row{.key=key, .label=name, .help=help, .count=Two,
        .get=[](const Config &c) { return c.*member ? 1 : 0; },
        .set=[](Config &c, int n) { c.*member = n != 0; }, .names="Off|On"};
}
Row EmptyRow() {
    return Row{.key = "mods.empty", .label = "No mods installed",
        .help = "Copy mod packs into\n/data/darkcloud/mods\nthen reopen Options.",
        .count = One, .get = [](const Config &) { return 0; }, .set = [](Config &, int) {}, .names = "-"};
}
}
void PrepareModMenu() {
    rows.clear();
    strings.clear();
    selection_errors.clear();
    rows.push_back(Row{.key="mods.weapon_browser", .label="Weapon Evolution", .help="Browse weapon paths\nand stat requirements.\nOpen from a loaded\ngame's pause menu.",
        .count=One, .get=[](const Config &) { return 0; }, .set=[](Config &, int) {}, .names="Open",
        .activate=OpenWeaponBrowser});
    rows.push_back(Native<&Config::town_autosave>("mods.town_autosave", "Town Autosave",
        "Save on town arrival\nwithout a starting event.\nUses its own Auto slot;\nkeeps numbered saves."));
    rows.push_back(Native<&Config::swap_keeps_lock_on>("game.swap_keeps_lock_on", "Swap Keeps Lock-On",
        "Native community mod.\nQuick character swaps\nkeep a valid enemy\nlocked on."));
    rows.push_back(Native<&Config::element_quick_select>("game.element_quick_select", "Element Quick Select",
        "D-pad Up in a dungeon\nopens the element\nselector."));
    rows.push_back(Native<&Config::qte_always_win>("game.qte_always_win", "Always Win QTEs",
        "Button prompt events\nend in a perfect result."));
    rows.push_back(Native<&Config::character_replace_custom_name>("mods.character_replace_custom_name", "Use Pack's Name",
        "On: a character pack\nalso replaces custom\nnames for display.\nApplies at title."));
    const auto directory = PathsSaveRoot().parent_path() / "mods";
    const auto installed = DiscoverMods(directory);
    for (const auto &mod : installed) {
        const auto id = mod.id;
        auto help = mod.supported ? "Version " + mod.version + "\nApplies at title.\n" + mod.description
                                  : "This mod cannot load.\n" + mod.problem;
        for (const auto &d : mod.requires_mods) help += "\nNeeds " + d.id + (d.version.empty() ? "" : " " + d.version);
        for (const auto &id : mod.conflicts) help += "\nConflicts: " + id;
        Row row{.key = Keep("mods.pack." + id), .label = Keep(mod.name), .help = Keep(help),
            .count = mod.supported ? Two : One,
            .get = [id, supported=mod.supported](const Config &c) { return supported && ModRequested(c, id) ? 1 : 0; },
            .set = [id, directory, supported=mod.supported](Config &c, int n) {
                if (!supported) return;
                Config candidate = c;
                SetModRequested(candidate, id, n != 0);
                const auto checked = LoadModPackages(directory, candidate.enabled_mods);
                bool valid = checked.enabled.size() == candidate.enabled_mods.size();
                if (!valid) {
                    selection_errors[id] = checked.messages.empty() ? "Invalid selection" : checked.messages.front();
                    std::fprintf(stderr, "mods: selection refused: %s\n", selection_errors[id].c_str());
                    return;
                }
                selection_errors.clear();
                c = std::move(candidate);
            },
            .text = [id, supported=mod.supported](const Config &c) {
                if (!supported) return std::string("Unavailable");
                if (selection_errors.contains(id)) return std::string("Blocked");
                return std::string(ModRequested(c, id) ? "On" : "Off") + (ModChangePending(c, id) ? " *" : "");
            },
            .names = "Off|On",
            .restore = [id, supported=mod.supported](Config &c, const Config &) { if (supported) SetModRequested(c, id, false); }};
        rows.push_back(std::move(row));
    }
    if (installed.empty()) rows.push_back(EmptyRow());
}
std::span<const Row> ModRows() {
    if (rows.empty()) rows.push_back(EmptyRow());
    return rows;
}
} // namespace options
