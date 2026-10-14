#include "settings.hpp"

#include <cstdlib>
#include <string>

namespace {

std::string g_env_prefix;

} // namespace

void SettingsSetEnvPrefix(std::string_view prefix) { g_env_prefix.assign(prefix); }

const char *SettingsEnv(std::string_view name) {
    if (g_env_prefix.empty()) {
        return nullptr;
    }
    const std::string key = g_env_prefix + "_" + std::string(name);
    return std::getenv(key.c_str());
}
