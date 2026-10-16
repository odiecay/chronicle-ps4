// Lookup layout adapted from Jeffiest/Chronicle PR #187, head 3c8a816a.
// Kept separate from game state and PNG decoding so pack precedence is testable.
#include "texture_assets.hpp"
#include "platform/paths.hpp"
#include <cstdio>
#include <map>
namespace fs = std::filesystem;
namespace {
std::vector<fs::path> g_roots;
bool g_roots_set;
std::map<std::string, bool> &Reported() {
    static std::map<std::string, bool> reported;
    return reported;
}
fs::path Find(const fs::path &directory, std::string_view name, unsigned width, unsigned height) {
    std::string sized = std::string(name) + "_" + std::to_string(width) + "x" + std::to_string(height) + ".png";
    for (const std::string &file : {sized, std::string(name) + ".png"}) {
        std::error_code error;
        fs::path        path = directory / file;
        if (fs::is_regular_file(path, error)) {
            return path;
        }
    }
    return {};
}


}

std::vector<fs::path> TextureAssetRoots() {
    if (g_roots_set) {
        return g_roots;
    }
    std::vector<fs::path> roots;
    roots.push_back(PathsSaveRoot() / "assets");
    fs::path exe = PathsExecutable();
    if (!exe.empty()) {
        roots.push_back(exe.parent_path() / "assets");
    }
    return roots;
}

void TextureAssetsSetRoots(const std::vector<fs::path> &roots) {
    g_roots = roots;
    g_roots_set = !roots.empty();
}

fs::path TextureAssetPath(std::string_view area, std::string_view language, std::string_view name, unsigned width,
                          unsigned height) {
    if (name.empty() || name.find_first_of("/\\:") != std::string_view::npos || name == "." || name == "..") {
        return {};
    }
    auto safe = [](std::string_view folder) {
        return !folder.empty() && folder != "." && folder != ".." && folder.find_first_of("/\\") == std::string_view::npos;
    };
    fs::path winner;
    for (const fs::path &root : TextureAssetRoots()) {
        fs::path selected;
        for (std::string_view folder : {area, language}) {
            if (safe(folder)) {
                fs::path found = Find(root / std::string(folder) / "textures", name, width, height);
                if (!found.empty()) {
                    selected = found;
                    break;
                }
            }
        }
        if (selected.empty()) selected = Find(root / "textures", name, width, height);
        if (selected.empty()) continue;
        if (winner.empty()) {
            winner = selected;
        } else {
            static unsigned reported_conflicts = 0;
            const auto key = winner.string() + " overrides " + selected.string();
            if (reported_conflicts < 32 && !Reported()[key]) {
                Reported()[key] = true;
                ++reported_conflicts;
                std::fprintf(stderr, "mods: texture conflict %.*s; using %s over %s\n", int(name.size()), name.data(),
                             PathsDisplay(winner).c_str(), PathsDisplay(selected).c_str());
            }
        }
    }
    return winner;
}

