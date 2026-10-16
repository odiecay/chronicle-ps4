#include "mod_character.hpp"
#include "savedata.hpp"
#include "battle_globals.hpp"
#include "menu_draw.hpp"
#include <cstring>

namespace { ModCharacterIdentity identity; }
void ConfigureModCharacter(std::string_view name, bool replace_custom) { identity.Configure(name, replace_custom); }
void ModCharacterLoaded(CSaveData &save) {
    ModName name;
    std::memcpy(name.data(), save.GetCharaName(0), sizeof(name));
    identity.Loaded(name);
    std::memcpy(save.GetCharaName(0), name.data(), sizeof(name));
}
void ModCharacterForSave(CSaveData &save) {
    ModName name;
    std::memcpy(name.data(), save.GetCharaName(0), sizeof(name));
    name = identity.ForSave(name);
    std::memcpy(save.GetCharaName(0), name.data(), sizeof(name));
}
// Called by the port's NameDefaultSet after retail's language-specific defaults.
void ModCharacterDefaulted(int chara_no) {
    if (chara_no != 0 || !SaveData) return;
    ModName name;
    std::memcpy(name.data(), SaveData->GetCharaName(0), sizeof(name));
    identity.Defaulted(name);
    std::memcpy(SaveData->GetCharaName(0), name.data(), sizeof(name));
    NameSelect.name_pos = 0;
    while (NameSelect.name_pos < 10 && name[NameSelect.name_pos]) ++NameSelect.name_pos;
}

// Retail default names are retained for every other party member and language.
PORT_OVERRIDE void NameDefaultSet(int chara_no) {
    int language = GetMenuLangFlag();
    s16 default_names[7][6][11] = {
        {{20, 1, 46},          {12, 47, 5},          {60, 43, 222},        {41, 77, 222},        {3, 46, 56, 56},                {5, 63, 35, 46, 70}           },
        {{181, 202, 188, 201}, {185, 196, 188, 202}, {168, 202, 205, 202}, {179, 208, 189, 212}, {182, 201, 194, 188, 194, 188}, {176, 206, 200, 202, 201, 191}},
        {{181, 202, 188, 201}, {185, 196, 188, 202}, {168, 202, 205, 202}, {179, 208, 189, 212}, {182, 201, 194, 188, 194, 188}, {176, 206, 200, 202, 201, 191}},
        {{181, 202, 188, 201}, {185, 196, 188, 202}, {168, 202, 205, 202}, {179, 208, 189, 212}, {182, 201, 194, 188, 194, 188}, {176, 206, 200, 202, 201, 191}},
        {{181, 202, 188, 201}, {185, 196, 188, 202}, {168, 202, 205, 202}, {179, 208, 189, 212}, {182, 201, 194, 188, 194, 188}, {176, 206, 200, 202, 201, 191}},
        {{181, 202, 188, 201}, {185, 196, 188, 202}, {168, 202, 205, 202}, {179, 208, 189, 212}, {182, 201, 194, 188, 194, 188}, {176, 206, 200, 202, 201, 191}},
        {{181, 202, 188, 201}, {185, 196, 188, 202}, {168, 202, 205, 202}, {179, 208, 189, 212}, {182, 201, 194, 188, 194, 188}, {176, 206, 200, 202, 201, 191}},
    };
    s16 *name = SaveData->GetCharaName(chara_no);
    int  length;

    for (length = 0; default_names[language][chara_no][length] != 0 && length < 10; length++) {
        name[length] = default_names[language][chara_no][length];
    }

    NameSelect.name_pos = length;

    for (; length < 32; length++) {
        name[length] = 0;
    }
    ModCharacterDefaulted(chara_no);
}
