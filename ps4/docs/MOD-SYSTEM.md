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

## PS4 manifest rejection, APP_VER 01.05

The first hardware menu test rejected Tora as `invalid manifest`. FTP readback
confirmed the same 81,529-byte manifest and SHA-256 as the validated Windows
pack. Linked ELF disassembly identified a platform mismatch in containment:
OpenOrbis' musl `realpath` uses Linux O_PATH and /proc/self/fd; its `lstat`
delegates to a `fstatat` stub which unconditionally returns ENOSYS. These
implementations cannot establish canonical mod paths on PS4.

PS4 now checks absolute, normalized path ancestry and refuses a symlink in any
component. A narrow platform helper resolves the real native `lstat` service
from loaded modules using GetModuleList/Dlsym, without changing global libc
link order, guessing a module handle, issuing raw syscalls or disabling path
containment. Resolution failure remains a rejection and is logged. Windows
retains canonical-path handling. Manifest path rejection, unreadable/oversized
JSON and invalid metadata now have distinct persistent messages.

Six portable suites pass, the PS4 component-walk policy passes the host loader
contracts, and the real Tora pack's 310 paths pass that policy. This does not
prove the native service on hardware: the installed 01.05 candidate logs its
resolution and awaits Codey's next menu test. Evidence, original/revised ELF
disassembly, packages, transfer readback and installation receipts are under
`C:/Users/Codey/AppData/Local/Cole/PS4/chronicle-mod-path-fix-v05-20261016`.

## HD texture storage, APP_VER 01.06

01.05 hardware confirmed Tora activation and the native path service. The
TickleMyFungus pack was then adapted by exact original texture/palette hashes:
2,176 native paths, up to 2x resolution, 1024-pixel cap, PS2 alpha normalized.
It is installed separately under `/data/darkcloud/mods/dark-cloud-upscaled-ps4`.

The first HD hardware log confirmed replacements loading, but Piglet reached
its configured 236 MiB flexible-memory limit. Image allocations and 1080p menu
render targets failed. RGBA replacements also discarded source CLUTs, causing
an indexed image-copy rejection. The reported floor colours remain a visual
observation; their precise cause has not yet been confirmed on hardware.

01.06 retains indexed storage and the original palette for indexed PS4 source
textures. HD texels are mapped to that palette using premultiplied colour and
alpha distance. This uses one byte instead of four per image texel and restores
the existing palette-update/copy path. It retains the original colour range;
it does not provide arbitrary full-colour art on those indexed textures.
Desktop behaviour stays unchanged. `DC_TEXTURE_SOURCE_PALETTE` allows an
isolated desktop build to exercise the same policy.

The standalone regression matches exhaustive search for 8,192 samples against
each of 16- and 256-entry palettes, including transparent edges. An isolated
Windows game executable rendered 180 Norune ticks with the actual pack and
this policy; its log confirms indexed HD replacements. Earlier fixture runs
used the wrong mod directory and did not activate HD; they are preserved and
are not validation evidence. PS4 compilation, PKG digest/signature validation,
FTP readback and installation passed. Hardware memory and floor validation is
pending a full close/relaunch. Evidence and the preserved pre-change ELF are
in `C:/Users/Codey/AppData/Local/Cole/PS4/chronicle-hd-palette-fix-v06-20261016`.
