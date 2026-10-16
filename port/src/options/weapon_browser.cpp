#include "options/weapon_browser.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "battlemenu.hpp"
#include "gamepad.hpp"
#include "gametext.hpp"
#include "itemdata.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp"
#include "menu_inventory.hpp"
#include "menu_misc.hpp"
#include "menu_option.hpp"
#include "menu_save.hpp"
#include "menuetc.hpp"
#include "options/screen.hpp"
#include "savedata.hpp"
#include "snd.hpp"
#include "weapon_evolution.hpp"
#include "weapon_evolution_game.hpp"

namespace options {
namespace {
bool open;
int held_index, selected, first, requirement_page;
std::vector<WEAPON_HAVE> held;
std::vector<evolution::Weapon> catalog;
std::vector<evolution::Node> tree;
std::array<std::unique_ptr<GameText>, 30> labels;
constexpr const char *stat_names[] = {"Attack", "Magic", "Fire", "Ice", "Thunder", "Wind", "Holy",
    "Dragon", "Undead", "Marine", "Rock", "Plant", "Beast", "Sky", "Metal", "Mimic", "Mage"};
const evolution::Weapon *Definition(int id) {
    auto it = std::find_if(catalog.begin(), catalog.end(), [id](const auto &w) { return w.id == id; });
    return it == catalog.end() ? nullptr : &*it;
}
// The same system message table the game's item labels use. This is live game
// text (including the selected language), not a second list of weapon names.
std::string Name(int id) {
    const auto *info = GetCommonItemInfo(id);
    if (info && CommonMenuMes2.buff_system) {
        if (const s16 *text = CommonMenuMes2.GetTextLineDataTop_system(info->msg)) {
            std::vector<s16> codes;
            for (int n = 0; n < 100 && text[n] != MES_CODE_END; ++n) codes.push_back(text[n]);
            codes.push_back(MES_CODE_END);
            return GameTextDecode(codes.data());
        }
    }
    return "Weapon " + std::to_string(id);
}
void Text(int slot, std::string text, int x, int y, int width) {
    if (!labels[slot]) { labels[slot] = std::make_unique<GameText>(); labels[slot]->KeepGameRandom(true); }
    labels[slot]->Set(text);
    labels[slot]->FitWidth(width);
    labels[slot]->Draw(x, y);
}
void Rebuild() {
    selected = first = requirement_page = 0;
    tree = held.empty() ? std::vector<evolution::Node>{} : evolution::Tree(catalog, held[held_index].item_no);
}
}
void OpenWeaponBrowser() {
    open = true;
    held.clear(); catalog.clear(); held_index = 0;
    for (auto &label : labels) label.reset();
    if (g_screen.mode != OPTION_OPEN_TITLE && SaveData) {
        const auto *status = SaveData->GetDngStatus();
        for (const auto &weapons : status->chara_weapons) for (const auto &weapon : weapons) {
            if (weapon.item_no >= ITEM_WEAPON_START && weapon.item_no < ITEM_WEAPON_END && GetWeaponData(weapon.item_no)) held.push_back(weapon);
        }
        catalog = WeaponEvolutionCatalog();
    }
    Rebuild();
    std::fprintf(stderr, "mods: weapon browser opened, %zu owned, %zu definitions\n", held.size(), catalog.size());
}
void CloseWeaponBrowser() { open = false; for (auto &label : labels) label.reset(); }
bool WeaponBrowserOpen() { return open; }
void WeaponBrowserKeys(std::uint16_t buttons) {
    if (buttons & PAD_CIRCLE) { CloseWeaponBrowser(); ComMenuSePlay(MENU_SOUND_REFUSE); return; }
    if (held.empty()) return;
    if (buttons & (PAD_L1 | PAD_R1)) {
        held_index = (held_index + int(held.size()) + ((buttons & PAD_L1) ? -1 : 1)) % int(held.size());
        Rebuild();
    }
    if (!tree.empty()) {
        const int old = selected;
        if (buttons & PAD_DOWN) selected = std::min(selected + 1, int(tree.size()) - 1);
        if (buttons & PAD_UP) selected = std::max(selected - 1, 0);
        if (selected != old) requirement_page = 0;
        if (selected < first) first = selected;
        if (selected >= first + 8) first = selected - 7;
    }
    if (buttons & PAD_RIGHT) ++requirement_page;
    if (buttons & PAD_LEFT) requirement_page = std::max(0, requirement_page - 1);
}
void DrawWeaponBrowser() {
    MenuHelpWinDraw(40, 36, 32.0f, 23.0f, 128);
    Text(0, "Weapon Evolution", 60, 44, 520);
    Text(1, "{L1} {R1} weapon   Up/Down path   {circle} Back", 60, 73, 520);
    if (held.empty()) { Text(2, "Load a game, then open Mods from the pause menu.", 60, 130, 520); return; }
    Text(2, Name(held[held_index].item_no) + "  " + std::to_string(held_index+1) + "/" + std::to_string(held.size()), 60, 103, 520);
    if (tree.empty()) { Text(3, "No evolution data for this weapon.", 60, 140, 520); return; }
    for (int row = 0; row < 8 && first + row < int(tree.size()); ++row) {
        const auto &node = tree[first+row];
        const int indent = std::min(node.depth, 6) * 10;
        Text(3+row, std::string(first+row == selected ? "> " : "  ") + Name(node.id) + (node.cycle ? " [cycle]" : ""),
             60+indent, 140+row*23, 265-indent);
    }
    const auto &node = tree[selected];
    const auto *target = Definition(node.id);
    Text(11, Name(node.id), 340, 140, 235);
    if (node.parent == 0) {
        Text(12, "Current weapon.\nSelect a branch to\ninspect its requirements.", 340, 175, 235);
    } else if (const auto *parent = Definition(node.parent); parent && parent->special_next) {
        Text(12, "Mardan Garayan caught:\n" + std::to_string(GetFishMardanGarayanNum()) + " / " + std::to_string(parent->required_fish), 340, 175, 235);
    } else if (target) {
        WEAPON_HAVE effective{};
        WeaponAllValueSet(&held[held_index], &effective, 1);
        const auto requirements = evolution::Requirements(WeaponEvolutionStats(effective), target->requirements);
        const int pages = std::max(1, (int(requirements.size())+5)/6);
        requirement_page = std::clamp(requirement_page, 0, pages-1);
        Text(12, "Current / required", 340, 169, 235);
        for (int row = 0; row < 6 && requirement_page*6+row < int(requirements.size()); ++row) {
            const auto &r = requirements[requirement_page*6+row];
            Text(13+row, std::string(r.met ? "+ " : "- ") + stat_names[r.stat] + " " + std::to_string(r.current) +
                (r.maximum ? " <= " : " / ") + std::to_string(r.limit), 340, 195+row*23, 235);
        }
        Text(19, "Left/Right Stats " + std::to_string(requirement_page+1) + "/" + std::to_string(pages), 340, 340, 235);
    }
    Text(20, "Compared with your current weapon, including attachments.", 60, 380, 520);
    Text(21, "Later evolutions can change stats. Viewing changes nothing.", 60, 405, 520);
}
} // namespace options
