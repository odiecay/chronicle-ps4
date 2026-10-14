# What is inside the package

`Chronicle-PS4-v0.1.0.pkg` holds 24 files, listed here with their SHA-256 so you can check them. This list was made
by unpacking the finished package, not from the build folder. It contains **no game data** and no disc content: that
comes from your own disc image the first time you start it.

## The game

| File | Bytes | SHA-256 |
|---|---|---|
| `eboot.bin` | 5,433,952 | `662ecc106b3780b9113c07f870dbd6ddba4ff61e1ab45335fa22568cb145f274` |

`eboot.bin` is this repository's source, built with OpenOrbis and wrapped by its `create-fself`. Build it yourself
with the steps in the [README](../../README.md); the file will differ byte for byte (the wrapper adds build-specific
data), but the same source and toolchain give the same code.

## Two Sony system libraries (the only third-party binaries)

| File | Bytes | SHA-256 |
|---|---|---|
| `assets/misc/libScePigletv2VSH.sprx` | 744,208 | `69d6b3adc85b6edf5208b7f18fad3b2638ae7c4648f78880877bae3aa4202efd` |
| `assets/misc/libSceShaccVSH.sprx` | 10,394,272 | `0a64982b0d7e33701745ab5180a1d314c11980215e418d14e868c75de3ca1e12` |

These are Sony's `libScePigletv2VSH.sprx` (the OpenGL ES driver the PS4 draws with) and `libSceShaccVSH.sprx` (its
shader compiler), **unmodified**: the sizes and hashes are the published stock values for these files (the Ship of
Harkinian PS4 port's install guide lists the same ones). They are bundled so that the package works without a manual
step. The same two files are in this repository under `ps4/redist-sony/signed/`, so you can build the identical package
yourself (`python ps4/package.py --bundle-sprx ps4/redist-sony/signed`). To avoid them, build without `--bundle-sprx`
and put your own copies in `/data/self/system/common/lib/`; the game looks there if the package has none.

## From the OpenOrbis toolchain

| File | Bytes | SHA-256 |
|---|---|---|
| `sce_module/libc.prx` | 52,800 | `39ad53672bb0b14895f8465eb1619478ab935f5afe5dbf69c15f02cfc0a75ce6` |
| `sce_module/libSceFios2.prx` | 52,816 | `3f8236c5996cf8e9917b8d27706d9743f6603d89de3b9859eacbb9f466b19c01` |
| `sce_sys/about/right.sprx` | 102,864 | `e3416f9c6152276b494982baf7b65ebcd8052274ffc42f95a776318022842b64` |

The OpenOrbis Piglet sample's runtime modules and about-page file, as every OpenOrbis package carries.

## Package metadata

| File | Bytes | SHA-256 |
|---|---|---|
| `sce_sys/keystone` | 96 | `d09eb8ab5f3b700164fc8ad85c54c2acbad71244e942aeedf7e3e7eb56408aac` |

`param.sfo` carries the title (Chronicle PS4 (Dark Cloud)), the id `DCLD00001` and the content id. `icon0.png` is the
Chronicle moon artwork on a dark background.

## Text, symbols and music

* `lang/` (5 files): the port's translated strings and the Dark Cloud community message font
  (`font.ttf`, by Dayuppy and Moonbunny), from Chronicle.
* `glyphs/` (10 files): the button symbols, including a touch pad symbol drawn for this port.
* `music/` (2 files): two 8-bit remixes of the Dark Cloud soundtrack by **FlameCheese**, played during
  setup. They are free to use in any project with credit.

| File | Bytes | SHA-256 |
|---|---|---|
| `music/norune-village.ogg` | 1,585,518 | `7fa5ecbfcb684ca0b9ab8f637eabb263835d9803dcb925bd02b23100ea30bfd0` |
| `music/the-daily-life.ogg` | 1,764,371 | `917977fd39b30bc2922aea5d4f378cbf55732d2cee62d75cfd9dc1ecbdbb015d` |
