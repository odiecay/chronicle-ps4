#include "mod_character.hpp"
#include <algorithm>

namespace {
// Retail's name-entry alphabet, not the message font and not ASCII.
ModName Encode(std::string_view text) {
    ModName out{};
    for (unsigned i = 0; i < text.size(); ++i) out[i] = text[i] >= 'a' ? text[i]-'a'+188 : text[i]-'A'+162;
    return out;
}
bool Equal(const ModName &a, const ModName &b) {
    for (unsigned i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}
}
bool ModCharacterIdentity::Configure(std::string_view name, bool replace_custom) {
    active_ = alias_ = false;
    replace_custom_ = replace_custom;
    if (name.empty()) return true;
    if (name.size() > 10 || !std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    })) return false;
    pack_ = Encode(name);
    active_ = true;
    return true;
}
void ModCharacterIdentity::Loaded(ModName &name) {
    original_ = name;
    alias_ = active_ && (replace_custom_ || Equal(name, Encode("Toan")));
    if (alias_) name = pack_;
}
void ModCharacterIdentity::Defaulted(ModName &name) {
    original_ = name;
    alias_ = active_;
    if (alias_) name = pack_;
}
ModName ModCharacterIdentity::ForSave(const ModName &current) const {
    return alias_ && Equal(current, pack_) ? original_ : current;
}
