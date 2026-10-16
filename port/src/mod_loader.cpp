#include "mod_loader.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
namespace {

bool IdValid(const std::string &id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}

nlohmann::json ReadManifest(const fs::path &path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > 65536) {
        return nullptr;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return nullptr;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return nlohmann::json::parse(text, nullptr, false);
}

bool Text(const nlohmann::json &j, const char *key) {
    return j.contains(key) && j[key].is_string() && !j[key].get_ref<const std::string &>().empty() &&
           j[key].get_ref<const std::string &>().size() <= 256;
}

bool VersionOne(const nlohmann::json &j) {
    return j.is_object() && j.contains("format") && j["format"].is_number_integer() && j["format"] == 1;
}

bool Contained(const fs::path &parent, const fs::path &child) {
    std::error_code ec;
    const auto base = fs::canonical(parent, ec);
    if (ec) return false;
    const auto resolved = fs::canonical(child, ec);
    if (ec) return false;
    const auto relative = resolved.lexically_relative(base);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

} // namespace

TextureMods LoadTextureMods(const fs::path &root) {
    TextureMods result;
    const auto order_path = root / "load-order.json";
    std::error_code ec;
    if (!fs::exists(order_path, ec) && !ec) {
        return result;
    }
    const auto order = ReadManifest(order_path);
    if (!VersionOne(order) || !order.contains("enabled") || !order["enabled"].is_array() ||
        order["enabled"].size() > 64) {
        result.messages.emplace_back("invalid load-order.json; no texture mods enabled");
        return result;
    }
    // Validate the order before loading any mod: malformed lists never activate a partial set.
    std::set<std::string> ids;
    for (const auto &value : order["enabled"]) {
        if (!value.is_string() || !IdValid(value.get_ref<const std::string &>()) ||
            !ids.insert(value.get<std::string>()).second) {
            result.messages.emplace_back("invalid or duplicate mod ID in load order; no texture mods enabled");
            return result;
        }
    }
    std::vector<std::string> enabled;
    for (const auto &value : order["enabled"]) enabled.push_back(value.get<std::string>());
    return LoadTextureMods(root, enabled);
}

bool TextureModIdsValid(const std::vector<std::string> &ids) {
    if (ids.size() > 64) return false;
    std::set<std::string> seen;
    for (const auto &id : ids) {
        if (!IdValid(id) || !seen.insert(id).second) return false;
    }
    return true;
}

TextureMods LoadTextureMods(const fs::path &root, const std::vector<std::string> &enabled) {
    TextureMods result;
    if (!TextureModIdsValid(enabled)) {
        result.messages.emplace_back("invalid enabled mod IDs; no texture mods enabled");
        return result;
    }
    std::error_code ec;
    for (const auto &id : enabled) {
        const auto folder = root / id;
        const auto manifest_path = folder / "mod.json";
        const auto manifest = ReadManifest(manifest_path);
        if (!Contained(root, manifest_path) || !VersionOne(manifest) ||
            !Text(manifest, "id") || manifest["id"] != id || !Text(manifest, "name") ||
            !Text(manifest, "version") || !Text(manifest, "game") || manifest["game"] != "dark-cloud" ||
            !Text(manifest, "type") || manifest["type"] != "texture-pack") {
            result.messages.push_back(id + ": missing, invalid, or unsupported manifest; disabled");
            continue;
        }
        const auto assets = folder / "assets";
        if (!fs::is_directory(assets, ec) || !Contained(folder, assets)) {
            result.messages.push_back(id + ": assets directory missing or outside the mod; disabled");
            continue;
        }
        result.enabled.push_back(TextureMod{id, manifest["name"].get<std::string>(),
                                            manifest["version"].get<std::string>(), assets});
    }
    std::reverse(result.enabled.begin(), result.enabled.end());
    return result;
}

std::vector<InstalledMod> DiscoverTextureMods(const fs::path &root) {
    std::vector<InstalledMod> installed;
    std::error_code ec;
    const auto options = fs::directory_options::skip_permission_denied;
    for (fs::directory_iterator it(root, options, ec), end; !ec && it != end; it.increment(ec)) {
        if (installed.size() == 64) break;
        const auto id = it->path().filename().string();
        if (!IdValid(id) || !it->is_directory(ec) || !fs::is_regular_file(it->path() / "mod.json", ec)) continue;
        InstalledMod entry;
        entry.id = id;
        entry.name = id;
        const auto manifest = ReadManifest(it->path() / "mod.json");
        if (VersionOne(manifest)) {
            if (Text(manifest, "name")) entry.name = manifest["name"].get<std::string>();
            if (Text(manifest, "version")) entry.version = manifest["version"].get<std::string>();
            if (Text(manifest, "type")) entry.type = manifest["type"].get<std::string>();
            if (Text(manifest, "description")) entry.description = manifest["description"].get<std::string>();
        }
        const auto validation = LoadTextureMods(root, {id});
        entry.supported = !validation.enabled.empty();
        if (!entry.supported) entry.problem = validation.messages.empty() ? "Invalid mod package" : validation.messages[0];
        installed.push_back(std::move(entry));
    }
    std::sort(installed.begin(), installed.end(), [](const InstalledMod &a, const InstalledMod &b) { return a.id < b.id; });
    return installed;
}
