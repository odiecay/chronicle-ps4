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
    for (const auto &value : order["enabled"]) {
        const std::string id = value.get<std::string>();
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
