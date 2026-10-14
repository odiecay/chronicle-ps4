# Chronicle PS4 (Dark Cloud): pre-release test plan

Build under test: package `2041d294…` (the `DarkCloud-PS4-01.00.pkg` on the USB stick and in `/data/pkg/`), 14 October 2026.
Console: PS4, firmware 13.52, GoldHEN (FTP 2121). Disc: Dark Cloud USA, NTSC 1.02 (`Dark Cloud (NTSC).iso`).

## The control

The **control is the configuration already proven to work**: the previous build (93) playing from data extracted on the PC (now saved as `/data/darkcloud/data.bak`). It runs, saves, and holds 57 to 61 ticks a second in the Divine Beast Cave.
Every test below changes **one variable** from the control. If something fails, the one thing that changed is the suspect.

| Variable | Control | Tests change it to |
|---|---|---|
| Where the package comes from | Package installer, earlier build | USB drive |
| Where the game data comes from | Extracted on a PC, copied by FTP | Extracted by the setup screen |
| Where the ISO is | n/a | USB `darkcloud/` folder, console `/data/darkcloud/`, USB root, USB subfolder |
| Disc | NTSC 1.02 | other versions (when ripped) |
| Sony modules | Copied by hand to `/data/self/system/common/lib/` | Bundled in the package |
| Extraction | n/a | uninterrupted, stopped with O, drive pulled |

## Before each run

- [ ] Game closed (quit from the home screen)
- [ ] Console state noted below (is `data` present, where is the ISO)
- [ ] After the run: tell me, and I read `/data/darkcloud/ps4_boot.log`

## Tests (run in this order)

### T1. USB route, clean (the main path)
Setup: `data` absent (it is `data.bak`), no ISO on the console, ISO in `darkcloud/` on the stick, package installed from the stick.

1. Plug the stick in, install the package with GoldHEN's Package Installer, launch from the home screen.
2. Expect the setup screen with the moon logo and "CHRONICLE PS4 (DARK CLOUD)".
3. Expect "DISC IMAGE FOUND!" and the file name. Let the countdown run (do not press X).
4. Expect the progress bar and "did you know" cards. Time it: ______ minutes.
5. Expect "YOUR GAME IS READY", press X, expect the game to start.

| Check | Result |
|---|---|
| Package installs from USB | |
| Setup screen letterboxed, readable, not overwhelming | |
| Found the ISO on the stick (the USB is readable) | |
| Countdown started the setup by itself | |
| Extraction finished, no error | |
| X starts the game with no relaunch | |
| Game plays (title, new game or load) | |

### T2. Game quality after a setup install
Same session as T1.
- [ ] Title screen, intro, a save slot works
- [ ] Walk around town, enter Divine Beast Cave: ticks a second ______ (expect about 60)
- [ ] Save, quit, relaunch: goes straight to the game (no setup screen)
- [ ] Load the save: it loads

### T3. FTP route
Setup: remove `data` again, stick unplugged, rename `Dark Cloud (NTSC).iso.bak` back to `.iso` in `/data/darkcloud/`.
- [ ] Setup screen finds the ISO on the console
- [ ] Rest as T1

### T4. Stop and resume
- [ ] Start setup, press O at about 30 percent (the screen says it continues next time)
- [ ] Relaunch: setup resumes from where it stopped and finishes, no error

### T5. Pull the drive during extraction
- [ ] Start setup from the stick, pull the stick at about 30 percent
- [ ] Expect a clear "setup did not finish" screen, not a hang or a crash
- [ ] Plug it back in, relaunch: it resumes

### T6. ISO placement
Each time: `data` absent, ISO only in the place listed.
- [ ] USB root (no `darkcloud` folder)
- [ ] USB subfolder two levels down
- [ ] Two ISOs (one real, one other game): only the real one is offered

### T7. Wrong or broken files
- [ ] A different PS2 game's ISO in `darkcloud/`: says it is not a Dark Cloud disc image, does not start
- [ ] A `.zip` of the disc where the ISO should be: says to unpack the ISO first
- [ ] Less than 2 GB free: says so, does not start

### T8. Bundled modules (the riskiest part)
Setup: rename `/data/self/system/common/lib/libScePigletv2VSH.sprx` and `libSceShaccVSH.sprx` to `.bak` so the only copies are the ones inside the package. Put them back afterwards.
- [ ] Game starts and draws normally
- [ ] Boot log line: `loaded module "/app0/assets/misc/libScePigletv2VSH.sprx"`
- [ ] If it fails: black screen, and the log line says why. This blocks the release.

### T9. Discs, one per run (when ripped)
For each version: put it in `darkcloud/`, remove `data`, run setup, then start a new game and play 5 minutes.

| Disc | Setup finishes | Boots to title | Plays | Notes |
|---|---|---|---|---|
| USA NTSC 1.02 (the control disc) | | | | |
| PAL (SCES-50295) | | | | |
| PAL review build (July 2001) | | | | |
| Japan (SCPS-15004) | | | | |

### T10. Existing player
Setup: `data` present (restore `data.bak`).
- [ ] Launch: no setup screen, straight into the game
- [ ] Saves from the old build load

### T11. Reinstall and update
- [ ] Install the package over itself: `data` and `save` untouched
- [ ] Delete the app, reinstall: `/data/darkcloud` untouched, no setup screen


## USB trial runs (the next session)

The stick is laid out as the instructions say: `DarkCloud-PS4-01.00.pkg` in the root, the ISO in `darkcloud/`.
Before each run the console is reset by me (game closed first): `data` set aside, and the console's own ISO renamed to `.iso.bak` unless the run says otherwise. Latest package on the stick and in `/data/pkg`: `9878e57f…`.

| Run | Console state before | Plug in | What to do | What I check afterwards |
|---|---|---|---|---|
| U1: USB, uninterrupted | no `data`, console ISO renamed away | stick | Launch. Let it run. Play into the cave | the `[setup] ftp:` and `copying` lines, 5,234 files, the copy deleted (`setup-copy.part` gone) |
| U2: internal copy | no `data`, ISO back as `.iso` in `/data/darkcloud/`, stick **out** | nothing | Launch. Let it run | same as the FTP route: 5,234 files |
| U3: both present | no `data`, console ISO as `.iso`, stick **in** | stick | Launch | it should use the console copy and not copy from the stick |
| U4: yank the stick during the copy | as U1 | stick | Pull the stick at about 30% of "copying" | a clear message, no crash, no half file left behind |
| U5: recover | after U4 | stick back in | Launch again | it finds the disc and carries on to the end |
| U6: yank during unpacking | as U1 | stick | Pull the stick once "unpacking" has started | unpacking does not need the stick any more, so it should carry on: expected to finish |
| U7: stop with O, then resume | as U1 | stick | Press O in "unpacking", launch again | it resumes and finishes |
| U8: new look and timing | as U1 | stick | Read the first screens | the first screen stays about 5 s with no countdown number; the did-you-know cards name Dark Cloud; the touch pad icon shows in the Georama menu |

Record what actually happens in the results table above, and copy the confirmed behaviour into `INSTALL.md` ("If something goes wrong").

## Results log

| Test | Date and time | Pass / Fail | What you saw | Log checked |
|---|---|---|---|---|
| T1 USB route | | not yet | The game cannot see USB at all (its `/mnt/usb0` does not exist). A loopback FTP bridge to GoldHEN's server is built, untested | |
| T3 FTP route, detection | 14 Oct 20:20 | PASS | ISO found within seconds, explanation, countdown, started | boot log |
| T3 FTP route, extraction (1st run) | 14 Oct 20:24 | FAIL, fixed | Crash CE-34878-0: a timed `condition_variable` wait throws on the PS4. Replaced by polling | `terminating with uncaught exception` |
| T3 FTP route, extraction (2nd run) | 14 Oct 20:30 | PASS with a defect | Extracted and started, but 47 files missing (console `std::format` ignored `{:02}`, printed chars as numbers), so the Divine Beast Cave crashed | `File open error "dun/script/d01/d01_1.mes"` |
| T2 / T10 self-repair of that data | 14 Oct 20:48 | PASS | New build rebuilt the files from `data.hd2` before launch; 5,234 files, none missing, none different from the PC extraction; cave entered | `[setup] the game files are complete` |
| T8 bundled modules | 14 Oct 20:20 | PASS | Signed copies loaded from `/app0/assets/misc/` (the decrypted copies failed with 0x80020008) | `loaded module "/app0/assets/misc/..."` |
| Setup music, backdrop, widescreen | 14 Oct 20:2x | PASS | Heard and seen on the console | user |
| T3 FTP route, clean re-run with fixed extractor (x2) | 14 Oct 20:58, 21:09 | PASS | Fresh setup twice from the ISO: 5,234 files identical to the repaired set, no repair screen, version note written, Divine Beast Cave entered both times, no crash | `[setup] done, starting the game` |
| U1 USB route, uninterrupted (+ part of U8) | 14 Oct 21:25 | PASS | Stick only source: loopback FTP bridge reached the drive from inside the game, copied the ISO to `setup-copy.part`, unpacked from it, deleted the copy; 5,234 files identical to the FTP-route install; version note written; cave entered; first screens readable, did-you-know cards name Dark Cloud, touch pad icon shows in the Georama menu. Feedback: the two stages were a little ambiguous, so the headers now say STEP 1 OF 2 / STEP 2 OF 2 | `[setup] ftp: reached, 1 disc image(s) on USB`, `copying /mnt/usb0/...`, `extracted ...` |
| Blind test (release package `4f46f233…`, fresh install from the USB, new player's defaults) | 14 Oct, evening | PASS | Setup from the stick, then played; held 60 ticks a second (mean 59.9, 99.5% of 1,292 half-minute windows at 55 or better) | boot log |
| New game opening and first field, 1080p | 15 Oct 00:5x | PASS without a recorder | A first run with Remote Play recording showed a slow cutscene and black patches on distant cliffs: the log showed the memory pool full and 44,870 framebuffer errors. Without the recorder: 0 memory errors, 0 framebuffer errors, 17.4 ms average over about 9,600 frames | boot log |
| Still to do | | | T1 USB (bridge), T3 clean re-run with the fixed extractor, T4, T5, T6, T7, T9, T11 | |

## What to send me after a run

1. "Done" and the test number.
2. What the screen did, in your own words (and a photo if something looks wrong).
3. I fetch `ps4_boot.log` and match it against the lines above. Lines that matter: `[setup] ...`, `[PS4] game data: ...`, `loaded module ...`, `[PS4] FATAL`.

## Release gate

Do not tag until T1, T2, T3, T4, T8 and T10 pass. T5, T6, T7 and T11 are expected to pass but would not stop a first preview if they only fail on the edges. T9 sets the "tested discs" list in the release notes.
