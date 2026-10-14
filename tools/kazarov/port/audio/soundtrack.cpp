#include "soundtrack.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <system_error>

#include "platform/settings.hpp"
#include "platform/paths.hpp"
#include "trace.hpp"

namespace audio {

namespace {

struct Entry {
    std::filesystem::path           file;
    float                           gain = 1.0f;
    bool                            loop = true;
    bool                            has_loop_start = false;
    double                          loop_start = 0.0;
    double                          loop_end = 0.0;
    bool                            failed = false;
    std::shared_ptr<const PcmTrack> loaded;
};

std::mutex                   g_mutex;
bool                         g_read = false;
std::filesystem::path        g_folder;
std::map<std::string, Entry> g_entries;

void ReadMapping() {
    g_read = true;
    g_entries.clear();
    g_folder = PathsSaveRoot().parent_path() / "soundtrack";
    std::ifstream file(g_folder / "soundtrack.json");
    audio::Trace("soundtrack: reading %s (%s)", (g_folder / "soundtrack.json").string().c_str(), file ? "found" : "missing");
    if (!file) {
        return;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false, true);
    if (root.is_discarded() || !root.is_object() || !root.contains("version") ||
        !root["version"].is_number_integer() || root["version"] != 1 || !root.contains("overrides") ||
        !root["overrides"].is_object()) {
        std::fprintf(stderr, "soundtrack: %s needs version 1 and an overrides object\n",
                     (g_folder / "soundtrack.json").string().c_str());
        return;
    }
    for (const auto &[name, value] : root["overrides"].items()) {
        if (!value.is_object() || !value.contains("file") || !value["file"].is_string()) {
            std::fprintf(stderr, "soundtrack: %s needs a \"file\"\n", name.c_str());
            continue;
        }
        if ((value.contains("gain") && (!value["gain"].is_number() ||
                                        !std::isfinite(value["gain"].get<double>()))) ||
            (value.contains("loop") && !value["loop"].is_boolean()) ||
            (value.contains("loop_start") && (!value["loop_start"].is_number() ||
                                              !std::isfinite(value["loop_start"].get<double>()))) ||
            (value.contains("loop_end") && (!value["loop_end"].is_number() ||
                                            !std::isfinite(value["loop_end"].get<double>())))) {
            std::fprintf(stderr, "soundtrack: %s has invalid playback settings\n", name.c_str());
            continue;
        }
        Entry entry;
        entry.file = g_folder / PathsFromUtf8(value["file"].get<std::string>());
        entry.gain = value.value("gain", 1.0f);
        entry.loop = value.value("loop", true);
        entry.has_loop_start = value.contains("loop_start");
        entry.loop_start = value.value("loop_start", 0.0);
        entry.loop_end = value.value("loop_end", 0.0);
        g_entries[name] = std::move(entry);
    }
}

} // namespace

bool SoundtrackEnsure() {
    const auto      folder = PathsSaveRoot().parent_path() / "soundtrack";
    const auto      path = folder / "soundtrack.json";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) {
        std::fprintf(stderr, "soundtrack: cannot create %s: %s\n", folder.string().c_str(), error.message().c_str());
        return false;
    }
    if (std::filesystem::exists(path, error)) {
        return true;
    }
    if (error) {
        std::fprintf(stderr, "soundtrack: cannot check %s: %s\n", path.string().c_str(), error.message().c_str());
        return false;
    }
    std::ofstream file(path);
    file << "{\n  \"version\": 1,\n  \"overrides\": {}\n}\n";
    file.close();
    if (!file) {
        std::fprintf(stderr, "soundtrack: cannot write %s\n", path.string().c_str());
        return false;
    }
    return true;
}

std::shared_ptr<const PcmTrack> SoundtrackFor(std::string_view sequence) {
    if (!SettingsGet().soundtrack) {
        return nullptr;
    }
    std::lock_guard lock(g_mutex);
    if (!g_read) {
        ReadMapping();
    }
    auto found = g_entries.find(std::string(sequence));
    if (found == g_entries.end()) {
        return nullptr;
    }
    Entry &entry = found->second;
    if (entry.loaded == nullptr && !entry.failed) {
        std::string error;
        auto        track = LoadPcmTrack(entry.file, error);
        if (track == nullptr) {
            std::fprintf(stderr, "soundtrack: %s: %s\n", entry.file.string().c_str(), error.c_str());
            entry.failed = true;
            return nullptr;
        }
        track->gain = entry.gain;
        track->loop = entry.loop;
        track->loop_start = entry.has_loop_start ? static_cast<std::int64_t>(entry.loop_start * track->rate) : 0;
        track->loop_end = static_cast<std::int64_t>(entry.loop_end * track->rate);
        entry.loaded = std::move(track);
    }
    return entry.loaded;
}

void SoundtrackReload() {
    std::lock_guard lock(g_mutex);
    g_read = false;
}

} // namespace audio
