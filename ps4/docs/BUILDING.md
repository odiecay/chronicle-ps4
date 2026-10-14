# Building the package yourself

This is for people who want to compile the port instead of downloading the release. These exact steps were tested on a **fresh clone of this repository** on Windows 11. You do **not** need a disc image to build; you need one only to play.

## What you need

| Tool | Tested with | Notes |
|---|---|---|
| Git | any | a plain `git clone`, no `--recursive`: everything is in this repository |
| CMake | 3.20 or newer | |
| Ninja | 1.12 | on your `PATH` |
| Python | 3.13 | 3.10 or newer should do |
| LLVM / clang | 18.1.8 | needs to cross-compile for `x86_64-pc-freebsd12-elf`, which stock clang does |
| [OpenOrbis PS4 toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases) | current release | gives `create-fself`, `create-gp4` and `PkgTool.Core.dll` (in `bin/windows`) and the PS4 headers and libraries |
| .NET runtime | 8 | runs `PkgTool.Core.dll`; any recent runtime works |
| SDL3 headers | 3.4.x | headers only: the PS4 build supplies its own SDL layer. Download an SDL3 release and point at its `include` folder |

The packaging step is Windows only, because it uses OpenOrbis's Windows tools.

## Steps

Open a terminal in the repository and set where your tools are (adjust the paths):

```
set OO_PS4_TOOLCHAIN=C:\tools\OpenOrbis\PS4Toolchain
set PS4_LLVM_BIN=C:\tools\llvm\bin
set DOTNET=C:\tools\dotnet\dotnet.exe
```

`DOTNET` can be left out if `dotnet` is already on your `PATH`.

Configure the PS4 build. `-DPLATFORM=PC` is required: the default target is the PS2 one, which wants a disc image.

```
cmake -S . -B port/build/ps4 -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=ps4/cmake/ps4-toolchain.cmake ^
  -DCMAKE_BUILD_TYPE=Release -DPLATFORM=PC -DDC_BUILD_TESTS=OFF ^
  -DDC_SDL3_INCLUDE=C:\tools\SDL3\include
```

Build it (the first build is a few minutes):

```
cmake --build port/build/ps4
```

This produces `port/build/ps4/darkcloud.elf`. Then make the installable package:

```
python ps4/package.py --bundle-sprx ps4/redist-sony/signed
```

The package ends up in `ps4/out/`. Leave out `--bundle-sprx` to build without the two Sony modules; the game then looks for them in `/data/self/system/common/lib/` on the console.

## Good to know

* **Your package will not match the release byte for byte.** The compiler embeds the folder you build in, and packages carry signing data and timestamps. The same source gives the same game.
* The repository already holds the renderer (`tools/kazarov`, [Kazarov](https://github.com/TheMoonPeople/Kazarov), MIT) and the two stock Sony modules (`ps4/redist-sony/signed`, hashes in [PACKAGE-CONTENTS.md](PACKAGE-CONTENTS.md)).
* More on how the port is put together: [`ps4/README.md`](../README.md).
