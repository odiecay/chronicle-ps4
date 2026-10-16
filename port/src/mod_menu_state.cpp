#include "mod_menu_state.hpp"
#include "mod_loader.hpp"
#include "platform/config.hpp"
#include <algorithm>

namespace {
std::vector<std::string> startup;
const std::vector<std::string> &Requested(const Config &config) {
    return config.mods_configured ? config.enabled_mods : startup;
}
bool Has(const std::vector<std::string> &ids, std::string_view id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}
void SetStartupModSelection(const std::vector<std::string> &active) { startup = active; }
bool ModRequested(const Config &config, std::string_view id) { return Has(Requested(config), id); }
bool ModActive(std::string_view id) { return Has(startup, id); }
bool ModChangePending(const Config &config, std::string_view id) { return ModRequested(config, id) != ModActive(id); }
void SetModRequested(Config &config, const std::string &id, bool enabled) {
    auto requested = Requested(config);
    const auto it = std::find(requested.begin(), requested.end(), id);
    if (enabled && it == requested.end()) requested.push_back(id);
    if (!enabled && it != requested.end()) requested.erase(it);
    if (!TextureModIdsValid(requested)) return;
    config.enabled_mods = std::move(requested);
    config.mods_configured = true;
}
