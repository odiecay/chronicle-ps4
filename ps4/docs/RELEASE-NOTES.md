# Release notes

## v0.1.1 — world-map progression fix and optional mods

PS4 application version: **01.07**. Install over v0.1.0, fully close the game,
and launch again from the PS4 home screen. Existing game data, settings and
save files are retained.

### Progression fix

Confirming a world-map destination from a town event could crash the game,
including the first journey from Norune to Matataki after defeating Dran.
The event kept a script-stack pointer in a 32-bit integer. Native PS4 and
desktop builds use 64-bit pointers; writing the destination through that
truncated address caused an access violation.

Native builds now retain the typed script-stack pointer through menu completion.
The PS2 build keeps its original representation. This fixes the shared event
travel path, including later uses of that path.

### Optional features since v0.1.0

- An Options / Mods page with persistent selections, QTE assistance and quick
  element selection.
- Optional town autosave in a separate Auto slot and lock-on preservation when
  changing characters, incorporating the credited upstream changes.
- A weapon evolution browser showing the game's build-up branches and requirements.
- External asset packs with dependency/conflict validation, title-screen activation,
  checked replacement sizes and reversible character-name defaults.
- Indexed HD texture handling on PS4 that retains the original palettes and reduces
  memory use compared with full RGBA replacements.

External mod packs, replacement game assets, disc data and personal saves are
not included in the PKG. See [MOD-SYSTEM.md](MOD-SYSTEM.md) for the implementation
details, attribution and the limits of the experimental pack support.

### Validation

- A native game regression exercised the actual world-map event, menu and script
  stack. Confirming Matataki crashed the old code with an access violation;
  the fixed code exited successfully and returned destination 2 to the script.
- The PS4 ELF linked. The package's hashes and signatures passed validation.
  FTP readback confirmed that the installed PKG matched the tested build.
- On a physical PS4 running GoldHEN / firmware 13.52, a post-Dran save loaded,
  normal gate/world-map travel reached Matataki, and the player confirmed movement.
  The console log recorded Matataki's scene and sound set loading.

This confirms the reported progression failure is fixed. A complete playthrough
and compatibility with every external mod pack have not been tested.
