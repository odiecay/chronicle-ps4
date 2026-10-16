# Private mod system engineering checkpoint

The released v0.1.0 package and the hardware-confirmed Mods-menu package are
preserved under NAS backups. Development is on `development/mod-loader-ps4`.
No game or mod artwork/audio/model data is committed to this repository.

Implemented: installed-pack Options rows; title-boundary external activation;
dependency/exact-version and conflict validation; original/replacement content
fingerprints; atomic binary overlays; reversible protagonist default/custom
names; a weapon evolution browser backed by retail masks and requirements;
optional town autosave and quick-swap lock-on (upstream PRs #185 and #170,
original authorship retained). Existing element picker and QTE assist also
appear in Mods. Town autosave defaults off and uses its own Auto slot.

Character defaults affect the live name; the save writer restores the original
name in its copy before computing checksums. A custom name edited in play is
retained. Pack/model/voice changes apply at the title screen. This is not a PS2 trainer
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

## Completed Tora integration, same session

The executable audit found exactly twelve changed bytes: six language rows of
the default-name table, each Toan -> Tora. No other executable bytes changed.
The native character-name hook supplies the equivalent behavior.

The private pack contains 310 changed assets (215,017,516 bytes), excluding
DATA.HD2 and extractor-generated languages.json. The original game directory
and archive remain intact. Credits: SomberTwilight and nachschreien9289; the
creator's trailer/download above is retained in external pack metadata.

Format 2 can opt into `arena_checked_growth`. Each entry records original and
replacement sizes/fingerprints. A replacement that exceeds the original
sector-rounded capacity is refused unless its destination is in an active
arena with sufficient writable capacity. Guards and retired mappings do not
count. This checks allocation bounds; it does not prove that every caller's
internal subdivisions or every modified scene is compatible. Manifests are
bounded to 256 KiB to accommodate this real 310-file pack. Stack/static buffers
retain the original capacity contract and cannot silently accept larger reads.

Verified Windows runs against the same original data: pack enabled renders
Tora, pack disabled renders Toan; both exit successfully after 180 ticks.
The browser was driven through real menu input and captured with Baselard's
evolution branches and signed requirements. Screenshots and exact input scripts
are in `tora-windows-pack-01`. An initial visual check caught and corrected the
system-message +100 name offset and the parent menu/help overlap.

Six portable suites pass. The expanded native selection ran 78 cases: 76 passed
and two debug-only arena cases skipped in RelWithDebInfo. Five targeted tests
were rerun successfully after the final startup-identity lifetime correction.
The PS4 package is APP_VER 01.03, title DCLD00001; hashes, validation, transfer
receipts and save backups are in the experiment root. Hardware Tora/browser
validation remains pending. Enhanced/Reforged gameplay overhauls and new sword
content have not been ported in this first batch.

## Title-screen activation, APP_VER 01.04

External pack and character-name selections remain frozen during gameplay.
Closing Options at the title with changes pending requests a normal title-mode
reload. At its drained boundary the game stops the old sounds, rebuilds the
validated asset overlay and texture roots, reloads global messages/sound tables,
and runs TitleInit again (which releases the old managed textures and recreates
the title's models). GlobalNameInit then establishes the selected defaults before
New Game's book/name entry. A selection made during gameplay waits until the
player returns to title. No application restart is required.

Windows evidence: an actual controller-input run started with zero packs,
opened title Options, enabled Tora, closed Options, activated all 310 overrides
at the title reload, and selected New Game in the same process. The captured
frame shows the opening book. The 35 selected native regression cases pass,
including real loader enable/disable/re-enable and deferred reads; saves and
default/custom name round trips remain covered. Evidence is under
`C:/Users/Codey/AppData/Local/Cole/PS4/chronicle-title-mod-reload-v04-20261016`.
Physical PS4 testing of this activation boundary is still pending.
