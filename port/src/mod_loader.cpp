#include "mod_loader.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#if defined(__PS4__) && !defined(_WIN32)
#include <sys/stat.h>
extern "C" int Ps4PathLstat(const char *, struct stat *);
#endif

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
    if (ec || size > 262144) {
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

bool Relative(std::string_view value) {
    if (value.empty() || value.size() > 240 || value.front() == '/' || value.back() == '/' ||
        value.find_first_of("\\:\0", 0, 3) != std::string_view::npos) return false;
    const fs::path path(value);
    for (const auto &part : path) if (part == "." || part == ".." || part.empty()) return false;
    return true;
}
bool Hash(const nlohmann::json &j, const char *key) {
    if (!j.contains(key) || !j[key].is_string()) return false;
    const auto &s = j[key].get_ref<const std::string &>();
    return s.size() == 16 && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool Contained(const fs::path &parent, const fs::path &child) {
    std::error_code ec;
#ifdef __PS4__
    // OpenOrbis' bundled realpath uses Linux O_PATH and /proc/self/fd. PS4
    // supplies neither contract, so filesystem::canonical rejects valid files.
    // Require an existing, symlink-free path instead: no component can redirect
    // an otherwise contained mod path outside its directory.
    const auto base = fs::absolute(parent, ec).lexically_normal();
    if (ec) return false;
    const auto resolved = fs::absolute(child, ec).lexically_normal();
    if (ec) return false;
    const auto relative = resolved.lexically_relative(base);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") return false;
    fs::path checked;
    for (const auto &component : resolved) {
        checked /= component;
#ifdef _WIN32
        // Host contract tests exercise the same component walk.
        const auto status = fs::symlink_status(checked, ec);
        if (ec || !fs::exists(status) || fs::is_symlink(status)) return false;
#else
        // libc's lstat also delegates to an ENOSYS fstatat stub. Resolve the
        // real libkernel service through the PS4 platform boundary instead.
        struct stat status{};
        if (Ps4PathLstat(checked.c_str(), &status) != 0 || S_ISLNK(status.st_mode)) return false;
#endif
    }
    return true;
#else
    const auto base = fs::canonical(parent, ec);
    if (ec) return false;
    const auto resolved = fs::canonical(child, ec);
    if (ec) return false;
    const auto relative = resolved.lexically_relative(base);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
#endif
}

std::optional<ModPackage> ReadPackage(const fs::path &root, const std::string &id, std::string &why) {
    auto fail = [&](const char *reason) -> std::optional<ModPackage> {
        why = id + ": " + reason;
        std::fprintf(stderr, "mods: %s\n", why.c_str());
        std::fflush(stderr);
        return {};
    };
    const auto folder = root / id;
    const auto path = folder / "mod.json";
    const auto j = ReadManifest(path);
    if (!Contained(root, path)) return fail("manifest path could not be verified");
    if (!j.is_object()) return fail("manifest unreadable, oversized or invalid JSON");
    if (!j.contains("format") || !j["format"].is_number_integer() ||
        !(j["format"] == 1 || j["format"] == 2) || !Text(j,"id") || j["id"] != id ||
        !Text(j,"name") || !Text(j,"version") || !Text(j,"game") || j["game"] != "dark-cloud" || !Text(j,"type"))
        return fail("invalid manifest");
    ModPackage p{id, j["name"].get<std::string>(), j["version"].get<std::string>(), folder / "assets"};
    p.type = j["type"].get<std::string>();
    if (j.contains("arena_checked_growth")) {
        if (!j["arena_checked_growth"].is_boolean() || j["format"] != 2) return fail("invalid resized-file policy");
        p.arena_checked_growth = j["arena_checked_growth"].get<bool>();
    }
    if (p.type != "texture-pack" && (j["format"] != 2 || (p.type != "asset-pack" && p.type != "character-pack")))
        return fail("unsupported package type or format");
    std::error_code ec;
    if (fs::exists(p.assets, ec) && (!fs::is_directory(p.assets, ec) || !Contained(folder, p.assets)))
        return fail("assets directory is outside the mod");
    if (p.type == "texture-pack" && !fs::is_directory(p.assets, ec)) return fail("assets directory missing");
    if (j.contains("requires")) {
        if (!j["requires"].is_array() || j["requires"].size() > 32) return fail("invalid dependencies");
        std::set<std::string> seen;
        for (const auto &d : j["requires"]) {
            if (!d.is_object() || !Text(d,"id") || !IdValid(d["id"]) || !seen.insert(d["id"]).second || d["id"] == id ||
                (d.contains("version") && !Text(d,"version"))) return fail("invalid dependency");
            p.requires_mods.push_back({d["id"].get<std::string>(), d.value("version", std::string{})});
        }
    }
    if (j.contains("conflicts")) {
        if (!j["conflicts"].is_array() || j["conflicts"].size() > 32) return fail("invalid conflicts");
        for (const auto &c : j["conflicts"]) {
            if (!c.is_string() || !IdValid(c.get<std::string>()) || c == id) return fail("invalid conflicting ID");
            p.conflicts.push_back(c.get<std::string>());
        }
        if (!ModIdsValid(p.conflicts)) return fail("duplicate conflicts");
    }
    if (p.type != "texture-pack") {
        if (!j.contains("files") || !j["files"].is_array() || j["files"].empty() || j["files"].size() > 512)
            return fail("asset packs need 1 to 512 declared files");
        std::set<std::string> paths;
        std::uint64_t total = 0;
        for (const auto &f : j["files"]) {
            if (!f.is_object() || !Text(f,"path") || !Relative(f["path"].get<std::string>()) ||
                !Text(f,"source") || !Relative(f["source"].get<std::string>()) ||
                !f.contains("size") || !f["size"].is_number_integer() || f["size"] <= 0 || f["size"] > 67108864 ||
                !Hash(f,"original_fnv1a64") || !Hash(f,"replacement_fnv1a64")) return fail("invalid replacement file");
            std::string game_path = f["path"].get<std::string>();
            std::transform(game_path.begin(), game_path.end(), game_path.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c;
            });
            const auto source = folder / f["source"].get<std::string>();
            const auto size = f["size"].get<std::uint64_t>();
            if (f.contains("original_size") && (!f["original_size"].is_number_integer() ||
                f["original_size"] <= 0 || f["original_size"] > 67108864)) return fail("invalid original file size");
            const auto original_size = f.value("original_size", size);
            if (!paths.insert(game_path).second || !fs::is_regular_file(source, ec) || !Contained(folder, source) ||
                fs::file_size(source, ec) != size || ec) return fail("missing, escaped, duplicate or wrong-size replacement");
            total += size;
            if (total > 536870912) return fail("replacement files exceed 512 MiB");
            if ((size+2047)/2048 > (original_size+2047)/2048 && !p.arena_checked_growth)
                return fail("larger files need explicit arena_checked_growth policy");
            p.files.push_back({game_path, source, size, f["original_fnv1a64"].get<std::string>(),
                f["replacement_fnv1a64"].get<std::string>(), original_size});
        }
    } else if (j.contains("files") || j.contains("character")) return fail("texture packs cannot replace binary files or character identity");
    if (p.type == "character-pack") {
        if (!j.contains("character") || !j["character"].is_object()) return fail("character identity missing");
        const auto &c = j["character"];
        if (!c.contains("slot") || !c["slot"].is_number_integer() || c["slot"] != 0 || !Text(c,"default_name")) return fail("only protagonist slot 0 is supported");
        const auto name = c["default_name"].get<std::string>();
        if (name.size() > 10 || !std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        })) return fail("default name must be 1 to 10 Latin letters");
        p.character = ModCharacter{0, name};
    } else if (j.contains("character")) return fail("character identity requires a character pack");
    return p;
}

} // namespace

ModPackages LoadModPackages(const fs::path &root) {
    ModPackages result;
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
    return LoadModPackages(root, enabled);
}

bool ModIdsValid(const std::vector<std::string> &ids) {
    if (ids.size() > 64) return false;
    std::set<std::string> seen;
    for (const auto &id : ids) {
        if (!IdValid(id) || !seen.insert(id).second) return false;
    }
    return true;
}

ModPackages LoadModPackages(const fs::path &root, const std::vector<std::string> &enabled) {
    ModPackages result;
    if (!ModIdsValid(enabled)) {
        result.messages.emplace_back("invalid enabled mod IDs; no texture mods enabled");
        return result;
    }
    for (const auto &id : enabled) {
        std::string problem;
        if (auto mod = ReadPackage(root, id, problem)) result.enabled.push_back(std::move(*mod));
        else result.messages.push_back(problem);
    }
    bool consistent = true;
    unsigned characters = 0;
    for (const auto &mod : result.enabled) {
        if (mod.character) ++characters;
        for (const auto &dependency : mod.requires_mods) {
            auto found = std::find_if(result.enabled.begin(), result.enabled.end(), [&](const auto &p) {
                return p.id == dependency.id && (dependency.version.empty() || p.version == dependency.version);
            });
            if (found == result.enabled.end()) {
                result.messages.push_back(mod.id + ": requires " + dependency.id + (dependency.version.empty() ? "" : " version " + dependency.version));
                consistent = false;
            }
        }
        for (const auto &id : mod.conflicts) {
            if (std::any_of(result.enabled.begin(), result.enabled.end(), [&](const auto &p) { return p.id == id; })) {
                result.messages.push_back(mod.id + ": conflicts with " + id);
                consistent = false;
            }
        }
    }
    if (characters > 1) { result.messages.emplace_back("Only one protagonist character pack can be enabled"); consistent = false; }
    if (!consistent) {
        result.messages.emplace_back("Inconsistent selection; no external packs activated");
        result.enabled.clear();
    }
    std::reverse(result.enabled.begin(), result.enabled.end());
    return result;
}

std::vector<InstalledMod> DiscoverMods(const fs::path &root) {
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
        if (manifest.is_object()) {
            if (Text(manifest, "name")) entry.name = manifest["name"].get<std::string>();
            if (Text(manifest, "version")) entry.version = manifest["version"].get<std::string>();
            if (Text(manifest, "type")) entry.type = manifest["type"].get<std::string>();
            if (Text(manifest, "description")) entry.description = manifest["description"].get<std::string>();
        }
        std::string problem;
        const auto package = ReadPackage(root, id, problem);
        entry.supported = package.has_value();
        if (package) { entry.requires_mods = package->requires_mods; entry.conflicts = package->conflicts; }
        else entry.problem = problem;
        installed.push_back(std::move(entry));
    }
    std::sort(installed.begin(), installed.end(), [](const InstalledMod &a, const InstalledMod &b) { return a.id < b.id; });
    return installed;
}
