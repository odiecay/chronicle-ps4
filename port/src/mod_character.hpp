#pragma once
#include <array>
#include <cstdint>
#include <string_view>

using ModName = std::array<std::int16_t, 32>;
// Keeps an appearance pack's default name out of the serialized save. Custom
// names remain custom; disabling a pack can therefore restore the original.
class ModCharacterIdentity {
public:
    bool Configure(std::string_view default_name, bool replace_custom = false);
    void Loaded(ModName &name);
    void Defaulted(ModName &name);
    ModName ForSave(const ModName &current) const;
private:
    ModName pack_{}, original_{};
    bool active_ = false, replace_custom_ = false, alias_ = false;
};
void ConfigureModCharacter(std::string_view default_name, bool replace_custom);
class CSaveData;
void ModCharacterLoaded(CSaveData &save);
void ModCharacterForSave(CSaveData &save);
