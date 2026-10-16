#include <gtest/gtest.h>
#include <algorithm>
#include "weapon_evolution_game.hpp"
#include "weapon_buildup.hpp"
#include "itemdata.hpp"
#include "userstatus.hpp"
#include "menu_inventory.hpp"
#include "menu_dungeon.hpp"
#include "savedata.hpp"
#include "mod_character.hpp"
#include "battle_globals.hpp"
#include "language.h"
#include "mainselect.hpp"

TEST(WeaponEvolution, CatalogMatchesRetailBuildUpDestinations) {
    CSaveData save;
    SaveData = &save;
    const auto catalog = WeaponEvolutionCatalog();
    ASSERT_FALSE(catalog.empty());
    ASSERT_TRUE(std::any_of(catalog.begin(), catalog.end(), [](const auto &w) { return w.id == ITEM_WEAPON_DAGGER; }));
    for (const auto &entry : catalog) {
        ASSERT_LT(entry.id, ITEM_WEAPON_END);
        WEAPON_HAVE held{};
        WepDataListToHaveCopy(entry.id, &held);
        WEP_BUILDUP_INFO retail[64]{};
        EnableBuildUpModel(retail, &held);
        std::vector<int> expected;
        for (int i = 0; i < 64 && retail[i].weapon_no >= 0; ++i) expected.push_back(retail[i].weapon_no);
        EXPECT_EQ(evolution::Destinations(catalog, entry.id), expected) << "item " << entry.id;
    }
}

TEST(WeaponEvolution, SignedRequirementsMatchRetailComparison) {
    for (const auto &entry : WeaponEvolutionCatalog()) {
        WEAPON_HAVE destination{}, held{}, effective{};
        WepDataListToHaveCopy(entry.id, &destination);
        WepDataListToHaveCopy(entry.id, &held);
        for (int probe : {0, 1, 15, 60, 99}) {
            held.attack = held.magic = probe;
            for (auto &v : held.elem) v = probe;
            for (auto &v : held.vs_monster) v = probe;
            WeaponAllValueSet(&held, &effective, 1);
            const auto current = WeaponEvolutionStats(effective);
            bool expected = true;
            for (unsigned i = 0; i < current.size(); ++i) expected &= evolution::Meets(current[i], entry.requirements[i]);
            EXPECT_EQ(CompareBuildUpModelData2(&destination, &held) != 0, expected) << entry.id << " probe " << probe;
        }
    }
}

TEST(CharacterPacks, DefaultNameEntryUsesPackAndRemainsReversible) {
    CSaveData save;
    SaveData = &save;
    LanguageCode = LANG_ENGLISH_UK;
    ConfigureModCharacter("Tora", false);
    NameDefaultSet(0);
    const ModName tora{181,202,205,188}, toan{181,202,188,201};
    EXPECT_EQ(std::memcmp(save.GetCharaName(0), tora.data(), sizeof(tora)), 0);
    EXPECT_EQ(NameSelect.name_pos, 4);
    ModCharacterForSave(save);
    EXPECT_EQ(std::memcmp(save.GetCharaName(0), toan.data(), sizeof(toan)), 0);
    ConfigureModCharacter({}, false);
}
