# Chronicle PS4 (Dark Cloud)

**Dark Cloud, running natively on a jailbroken PS4.** Not an emulator: this is the [Chronicle](https://github.com/TheMoonPeople/Chronicle) decompilation of Dark Cloud, built for the PS4 with a graphics layer written for it. It runs at 1080p and holds 60 frames a second.

**[Download the latest release](../../releases/latest)** · [Video](https://www.youtube.com/watch?v=EqCQ85O4dbw) · [Install guide](ps4/docs/INSTALL.md) · [What is inside the package](ps4/docs/PACKAGE-CONTENTS.md)

You bring your own copy of the game. Nothing from the disc is in this download.

## What you need

* A PS4 with [GoldHEN](https://github.com/GoldHEN/GoldHEN) (tested on firmware 13.52).
* Your own **Dark Cloud (USA, NTSC 1.02)** disc image as an `.iso`, about 1.7 GB. The PAL release is supported in the code but has not been tried on a console.
* A USB drive (exFAT or FAT32), or an FTP client.
* About 4 GB free on the PS4.

## Quick start

1. On a USB drive, make a folder named `darkcloud` and put your `.iso` in it. Put `DarkCloud-PS4-01.00.pkg` in the main folder of the drive.
2. Plug it into the PS4 and install the package with GoldHEN's Package Installer.
3. Start **Chronicle PS4 (Dark Cloud)** from the home screen and leave the drive plugged in.

The game finds your disc image and sets itself up. A few minutes later it says it is ready, and you press X to play. You only do this once. The [install guide](ps4/docs/INSTALL.md) has the FTP route and fixes for common problems.

## How it runs

Measured on a PS4 over a long session, in the Divine Beast Cave: an average of 59.9 ticks a second, with 99.5% of 1,292 half-minute windows at 55 or better. Logos are skipped and loading is shortened by default. Add an empty file named `ps4_show_intro` or `ps4_retail_loading` to `/data/darkcloud/` to get the original behaviour. The other options are listed in [`ps4/README.md`](ps4/README.md).

The first run writes a `config.json` in `/data/darkcloud/save` with the settings this port is tuned for: soft focus off (with it on, the heaviest scenes drop well below 60), mailbox presentation, a 60 fps cap and 1080p. Everything can still be changed in the game's options, and a `config.json` that is already there is never overwritten.

Your saves are in `/data/darkcloud/save` and stay on the console, including when you update the package.

## Known issues (v0.1.0, first preview)

* **Do not use Remote Play or a screen recorder while you play.** It takes memory the game needs, and the game can show black patches and slow cutscenes.
* Some menu backdrops may not draw. The graphics driver's memory pool is small, and the game says so in its log when it runs out.
* Shadows are drawn with a 16-bit depth buffer, the most the PS4 driver gives. Rarely, a shadow may look wrong (dark circles around Atla and chests have been seen once; this is not confirmed).
* Only the USA disc has been run on a console.

Please report problems with the log file (`/data/darkcloud/ps4_boot.log`) attached.

## About the package

The package carries two **unmodified Sony system libraries**, `libScePigletv2VSH.sprx` and `libSceShaccVSH.sprx`: the OpenGL ES driver and shader compiler the PS4 draws with. They are included so that the package works with no extra steps. They are also kept in this repository under [`ps4/redist-sony/signed/`](ps4/redist-sony/signed/) so you can build the same package yourself. Their hashes are in [`PACKAGE-CONTENTS.md`](ps4/docs/PACKAGE-CONTENTS.md).

## Building it yourself

You need the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), LLVM and CMake. Clone with `--recursive`, build the PS4 target, then make the package:

```
git clone --recursive https://github.com/odiecay/chronicle-ps4
cmake --build port/build/ps4
python ps4/package.py --bundle-sprx ps4/redist-sony/signed
```

The full steps, including how the toolchain is set up, are in [`ps4/README.md`](ps4/README.md). The original Chronicle readme, with its notes on the decompilation itself, is [`README-CHRONICLE.md`](README-CHRONICLE.md).

## Credits

* The **Chronicle** team ([TheMoonPeople](https://github.com/TheMoonPeople)) for the decompilation and the Kazarov build system. This port stands on their work.
* **Level-5** made Dark Cloud. This project is not affiliated with Level-5 or Sony.
* The **Ship of Harkinian PS4 port** showed how to do native PS4 homebrew with Piglet, and its install guide is the reference for the Sony modules.
* [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and [GoldHEN](https://github.com/GoldHEN/GoldHEN).
* **FlameCheese** for the 8-bit Dark Cloud remixes played during setup.

Chronicle and Kazarov keep their own terms (Kazarov is MIT). The PS4 layer in `ps4/` is MIT, see [`ps4/LICENSE-ps4-port`](ps4/LICENSE-ps4-port).
