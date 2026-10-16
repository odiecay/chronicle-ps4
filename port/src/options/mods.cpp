#include "options/rows.hpp"
#include "mod_loader.hpp"
#include "mod_menu_state.hpp"
#include "platform/paths.hpp"
#include <deque>

namespace options {
namespace {
std::vector<Row> rows;
std::deque<std::string> strings;
const char *Keep(std::string text) { return strings.emplace_back(std::move(text)).c_str(); }
int Two(const Config &) { return 2; }
int One(const Config &) { return 1; }
Row EmptyRow() {
    return Row{.key = "mods.empty", .label = "No mods installed",
        .help = "Copy mod packs into\n/data/darkcloud/mods\nthen reopen Options.",
        .count = One, .get = [](const Config &) { return 0; }, .set = [](Config &, int) {}, .names = "-"};
}
}
void PrepareModMenu() {
    rows.clear();
    strings.clear();
    const auto installed = DiscoverTextureMods(PathsSaveRoot().parent_path() / "mods");
    for (const auto &mod : installed) {
        const auto id = mod.id;
        const auto help = mod.supported ? "Version " + mod.version + "\nOn/Off applies after\nrestarting the game.\n" + mod.description
                                        : "This mod cannot load.\n" + mod.problem;
        Row row{.key = Keep("mods.pack." + id), .label = Keep(mod.name), .help = Keep(help),
            .count = mod.supported ? Two : One,
            .get = [id, supported=mod.supported](const Config &c) { return supported && ModRequested(c, id) ? 1 : 0; },
            .set = [id, supported=mod.supported](Config &c, int n) { if (supported) SetModRequested(c, id, n != 0); },
            .text = [id, supported=mod.supported](const Config &c) {
                if (!supported) return std::string("Unavailable");
                return std::string(ModRequested(c, id) ? "On" : "Off") + (ModChangePending(c, id) ? " *" : "");
            },
            .names = "Off|On",
            .restore = [id, supported=mod.supported](Config &c, const Config &) { if (supported) SetModRequested(c, id, false); }};
        rows.push_back(std::move(row));
    }
    if (rows.empty()) rows.push_back(EmptyRow());
}
std::span<const Row> ModRows() {
    if (rows.empty()) rows.push_back(EmptyRow());
    return rows;
}
} // namespace options
