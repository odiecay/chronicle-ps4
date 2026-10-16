# Private mod system engineering checkpoint

The released v0.1.0 package and the hardware-confirmed Mods-menu package are
preserved under NAS backups. Development is on `development/mod-loader-ps4`.
No game or mod artwork/audio/model data is committed to this repository.

Implemented: installed-pack Options rows; restart-aware external activation;
dependency/exact-version and conflict validation; original/replacement content
fingerprints; atomic binary overlays; reversible protagonist default/custom
names; a weapon evolution browser backed by retail masks and requirements;
optional town autosave and quick-swap lock-on (upstream PRs #185 and #170,
original authorship retained). Existing element picker and QTE assist also
appear in Mods. Town autosave defaults off and uses its own Auto slot.

Character defaults affect the live name; the save writer restores the original
name in its copy before computing checksums. A custom name edited in play is
retained. Pack/model/voice changes require restart. This is not a PS2 trainer
or an arbitrary native-code plugin interface.

Validation, 2026-10-16: the complete OpenOrbis PS4 ELF and an isolated Windows
application/test binary linked. Six portable contract suites pass. The native
Windows build passes 53 selected save/config/menu/evolution/character tests,
including all retail build-up destinations and real save/checksum round trips.
Two stale assertions were updated: the new Mods page and the preserved PS4
fork's FPS-overlay-off default. No production rendering default was changed.
Hardware validation of these new features is pending.

Runtime evidence and fresh builds:
`C:/Users/Codey/AppData/Local/Cole/PS4/chronicle-mod-system-v03-20261016`
`C:/Users/Codey/AppData/Local/Cole/PS4/build-chronicle-mod-system-v03-20261016`
`C:/Users/Codey/AppData/Local/Cole/PS4/build-chronicle-mod-system-windows-v03-20261016`

Tora's creator patch has now been obtained from the trailer description:
https://www.youtube.com/watch?v=zXhtiSwkv90
https://www.mediafire.com/file/0qpmjxyn0hbqt8t/DARKCLOUDTORAFINAL.xdelta/file
Patch SHA-256: 60083564632f2d5f52f4fc7438462cb7b43b839730eaa5c7d8c5abb420167386.
It applied to a separate copy of Codey's USA ISO. Both images extracted and
verified; 312 effective extracted files differ, 190 differ in size. An isolated
180-tick Windows town run with the patched data exited successfully and rendered
Tora; `tora-windows-direct-01/tora-town.png` is the genuine captured frame.
That proves this town's modified assets load in Chronicle, not toggle support
or full-game compatibility. The initial strict same-size overlay correctly
rejects the larger files. Next: establish a bounded arena read contract for
resized replacements, then compare enabled/disabled packs against original data.

`tools/create-asset-mod.py` creates new external packs from extracted trees.
It never modifies inputs or overwrites an output; executable/index patches
require separate native implementation. Metadata FNV-1a hashes identify asset
contents and are not authenticity signatures. PS2 code changes, if any, remain
to be audited separately from DATA.DAT assets.
