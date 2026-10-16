#pragma once
#include "weapon_evolution.hpp"
struct WEAPON_HAVE;
evolution::Stats WeaponEvolutionStats(const WEAPON_HAVE &weapon);
std::vector<evolution::Weapon> WeaponEvolutionCatalog();
