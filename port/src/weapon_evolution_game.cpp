#include "weapon_evolution_game.hpp"
#include "battlemenu.hpp"
#include "itemdata.hpp"
#include "menu_inventory.hpp"

evolution::Stats WeaponEvolutionStats(const WEAPON_HAVE &w) {
    evolution::Stats out{w.attack, w.magic};
    for (int i = 0; i < 5; ++i) out[2+i] = w.elem[i];
    for (int i = 0; i < 10; ++i) out[7+i] = w.vs_monster[i];
    return out;
}
std::vector<evolution::Weapon> WeaponEvolutionCatalog() {
    std::vector<evolution::Weapon> catalog;
    // END is one past ComItemInfo, not a weapon. GetWeaponData itself does not
    // bounds-check that table, so this boundary belongs to the caller.
    for (int id = ITEM_WEAPON_START; id < ITEM_WEAPON_END; ++id) {
        const auto *data = GetWeaponData(id);
        if (!data || data->owner >= 6) continue;
        const int position = id - GetDefaultWeaponNo(data->owner);
        if (position < 1 || position > MenuCharaWeaponMax[data->owner]) continue;
        WEAPON_HAVE required{};
        WepDataListToHaveCopy(id, &required);
        evolution::Weapon entry{id, data->owner, position, data->buildup_mask0, data->buildup_mask1, WeaponEvolutionStats(required)};
        if (id == ITEM_WEAPON_MARDAN_EINS || id == ITEM_WEAPON_MARDAN_TWEI) {
            entry.special_next = id + 1;
            entry.required_fish = id == ITEM_WEAPON_MARDAN_EINS ? 5 : 15;
        }
        catalog.push_back(entry);
    }
    return catalog;
}
