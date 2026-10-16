#pragma once
#include <cstdint>
namespace options {
void OpenWeaponBrowser();
void CloseWeaponBrowser();
bool WeaponBrowserOpen();
void WeaponBrowserKeys(std::uint16_t buttons);
void DrawWeaponBrowser();
} // namespace options
