# Chronicle PS4 (Dark Cloud)

**Play Dark Cloud on your jailbroken PS4.** A native port, not an emulator: 1080p, 60 frames a second.

## [⬇ Download the latest release](../../releases/latest)

[Watch it running](https://www.youtube.com/watch?v=EqCQ85O4dbw)

## Install in three steps

You need a PS4 with [GoldHEN](https://github.com/GoldHEN/GoldHEN), your own Dark Cloud (USA) disc image as an `.iso`, and a USB drive.

1. On the USB drive, make a folder called **`darkcloud`** and put your `.iso` in it. Put `DarkCloud-PS4-01.00.pkg` (from the download) in the drive's main folder.
2. Plug the drive into the PS4 and install the package with GoldHEN's **Package Installer**.
3. Start **Chronicle PS4 (Dark Cloud)** from the home screen and leave the drive plugged in.

The game sets itself up from your disc image. A few minutes later it says it's ready: press **X** and play. You only do this once.

**Need more?** [Full install guide](ps4/docs/INSTALL.md), with the FTP route and fixes for common problems.

## Good to know

* **No game data is included.** You bring your own disc image.
* **Don't run Remote Play or a screen recorder while you play.** It takes memory the game needs.
* Tested on firmware 13.52 with the **USA NTSC 1.02** disc. Other discs haven't been tried on a console.
* The package contains two unmodified Sony system files, so it works with no extra steps. [See exactly what's inside.](ps4/docs/PACKAGE-CONTENTS.md)

Your saves live in `/data/darkcloud/save` and stay on the console, including when you update.

## Known issues

This is the first preview (v0.1.0).

* Some menu backdrops may not draw. The graphics driver's memory pool is small.
* Shadows use a 16-bit depth buffer, the most the PS4 driver gives. Rarely a shadow may look wrong.
* The first run writes a tuned `config.json` (soft focus off, 60 fps cap, 1080p). Everything can be changed in the game's options, and an existing `config.json` is never overwritten.

Please report problems with the log attached: `/data/darkcloud/ps4_boot.log`.

## For developers

Everything needed to build the package is in this one repository, including the renderer and the two Sony modules. Install the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), then:

```
git clone https://github.com/odiecay/chronicle-ps4
cd chronicle-ps4
cmake --build port/build/ps4
python ps4/package.py --bundle-sprx ps4/redist-sony/signed
```

How it's put together, the options and the measurements are in [`ps4/README.md`](ps4/README.md). The original Chronicle readme is [`README-CHRONICLE.md`](README-CHRONICLE.md).

## Credits

* The **Chronicle** team ([TheMoonPeople](https://github.com/TheMoonPeople)) for the Dark Cloud decompilation and the Kazarov build system (MIT, in `tools/kazarov`). This port stands on their work.
* **Level-5** made Dark Cloud. This project is not affiliated with Level-5 or Sony.
* The **Ship of Harkinian PS4 port** showed how to do native PS4 homebrew with Piglet.
* [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and [GoldHEN](https://github.com/GoldHEN/GoldHEN).
* **FlameCheese** for the 8-bit Dark Cloud remixes played during setup.

The PS4 layer in `ps4/` is MIT ([`ps4/LICENSE-ps4-port`](ps4/LICENSE-ps4-port)). Chronicle and Kazarov keep their own terms.
