#pragma once
#include <string>
#include <string_view>
#include <vector>

struct Config;
void SetStartupModSelection(const std::vector<std::string> &active);
bool ModRequested(const Config &config, std::string_view id);
bool ModActive(std::string_view id);
bool ModChangePending(const Config &config, std::string_view id);
void SetModRequested(Config &config, const std::string &id, bool enabled);
