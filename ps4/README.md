# Chronicle PS4 (Dark Cloud)

A native port of the Chronicle decompilation of Dark Cloud (NTSC 1.02) to a jailbroken PS4 (GoldHEN). It is not an emulator: the decompiled game code runs as PS4 code, drawing through a new OpenGL ES 2
renderer on Sony's own Piglet GLES driver, reading the pad with `scePad` and playing sound through `sceAudioOut`.

Tested on one console only; compiling is not evidence that something runs, and every claim below that says "measured"
comes from the console's own log (`/data/darkcloud/ps4_boot.log`).

## What was built

| Piece | Where | What it does |
|---|---|---|
| GLES2 renderer | `tools/kazarov/port/gfx_gles/` | Replaces Chronicle's Vulkan renderer behind the same `gfx.hpp` API: display lists, 2D and mesh draws, render targets, blending, fog, palette textures. Matches the Vulkan output to under 1/255 mean difference on the test scenes. |
| PS4 platform layer | `ps4/src/` | Piglet/EGL host, pad (with touch pad), audio, user service, boot log, memory stats, crash signals, replacement for the few SDL calls the shared code makes. |
| Toolchain glue | `ps4/cmake/`, `ps4/compat/`, `ps4/package.py` | OpenOrbis cross build (clang, libc++ 11, FreeBSD ELF), a linker script `create-fself` accepts, libc/libc++ compatibility shims, `.pkg` packaging. |
| Port changes to the game | `port/src/` | PS4-only diagnostics, a direct `read()` file path, `GameSkipIntro`, save fixes. |

## Install

You need a jailbroken PS4 running GoldHEN and your own Dark Cloud disc image (an `.iso`; USA NTSC 1.02 is the tested one, the PAL release is supported by the port but untried on a PS4). No game data
is in the package.

1. Put the `.pkg` in the root of a USB drive (exFAT or FAT32) and your `.iso` in a folder called `darkcloud` on it, then plug it into the PS4.
2. Install the package with GoldHEN's Package Installer (it lists the `.pkg` files on the drive).
3. Start Dark Cloud from the home screen and follow the setup screen: it finds the ISO on the drive, checks it, and
   copies the game's files to `/data/darkcloud/data` (about 1.8 GB, a few minutes). Press X to close it when it says
   so, then start the game again. Setup happens once; the drive can be removed afterwards.

Without a USB drive, copy the `.iso` to `/data/darkcloud/` over FTP (GoldHEN's server, port 2121, binary mode); the
same screen picks it up. Launch from the home screen: launching through a control service crashes the shell UI with
"Invalid User Id".

To build it yourself: `cmake --build port/build/ps4`, then `python ps4/package.py`. Extracting by hand with
`dcdata extract <iso> /data/darkcloud/data` on a PC still works.

Saves are in `/data/darkcloud/save`, the log in `/data/darkcloud/ps4_boot.log`.

## Switches (empty files in `/data/darkcloud/`)

| File | Effect |
|---|---|
| `ps4_no_render_thread` | Render on the game thread instead of replaying and presenting each tick on a second thread (the default). Slower; for diagnosing. |
| `ps4_show_intro` | Play the Sony and Take 5 screens. By default they are skipped and the game starts at the title. |
| `ps4_720p` | Render at 1280x720 and let the console scale it (default is 1920x1080). |
| `ps4_retail_loading` | Keep retail's loading-screen holds (about 6.6 s) and the location captions that come up with them. By default the holds are cut to a few ticks, which makes the captions flash past. |
| `ps4_gpu_time` | Wait for the GPU before each swap and log the wait. Slows the game; for measuring only. |
| `ps4_stream_ring` | Use a ring buffer for per-draw vertex uploads. Off: it does not fit in Piglet's buffer memory. |
| `ps4_tune` | Live tuning, read every half second: lines `draw_limit=N`, `viewport_div=N`, `nearest=1`, `no_blend=1`, `skip_2d=1`, `skip_mesh=1`, `client_arrays=1`, `no_batch=1`, `timing=1` (per-phase CPU times in the log). |

## Controls

Cross/Circle/Square/Triangle and the shoulder buttons as on the PS2 pad; touch pad click is Select (the town overview
map); the touch pad also moves the menu finger. The Share button is owned by the system and cannot be mapped.

## Performance

Measured in the Divine Beast Cave, the heaviest scene tried (about 700 to 1400 draws, 1080p):

| Version | Ticks per second |
|---|---|
| First working build | about 20 |
| Batched vertex uploads | 28 to 38 |
| Plus render thread | 57 to 61 (mean over a full run: 59.5, 9 of 488 windows under 55) |

What the speed-up came from, in the order it was found:

1. **The clock.** OpenOrbis' `clock_gettime` passes Linux clock ids to a FreeBSD kernel, where id 1 is the process CPU
   clock, so every game timer ran on CPU time and loading screens crawled. `ps4/src/ps4_compat.c` maps the ids to the
   kernel's (realtime 0, CPU 2, monotonic 4).
2. **`sprintf` into its own buffer.** musl's `sprintf` overlapped source and destination; a dungeon crash. Replaced
   with a buffered version.
3. **Per-draw buffer uploads.** Every 2D draw and immediate mesh did two `glBufferData` calls, and each allocates inside
   Piglet (about 35 microseconds). Now all of them in a frame are gathered into one vertex and one index upload
   (`BeginImmediateBatch` in `gfx_gles/draw.cpp`), which took the vertex-data phase from 21 ms to under 4 ms a frame.
4. **The render thread** (`gfx_gles/renderthread.cpp`): the game records tick N+1 while another thread replays and
   presents tick N. Anything that is not recording (creating a texture, an immediate frame, a read-back) first waits for
   the render thread and takes the GL context back (`ApiBarrier`). The context is handed over with `eglMakeCurrent`
   at job boundaries, and a start-up self-test falls back to single-threaded rendering if the console refuses it.
5. **Lazy display targets**: the full-screen image and depth used only for interpolated in-between frames were costing
   16 MiB of Piglet's 236 MiB pool at 1080p, and full-screen effect targets were failing to allocate. They are now
   created the first time a display render needs them.

Smaller changes: uniform caching per program, render targets not zero-filled, a direct `read()` for game files,
SSE4.2 and Jaguar scheduling (`-msse4.2 -mtune=btver2`).

### Things that look like good ideas on this console and are not

* `-march=btver2` (AVX): the first build crashed at launch (SIGBUS "FPU device not available"). The system does not
  enable AVX, so code can use SSE4.2 and no more.
* Signals sent to the game thread (a sampling profiler): any signal kills the process with the same SIGBUS. Profile with
  timers inside the code (`ps4_tune` `timing=1`), not with signals.
* Client-side vertex arrays: the upload cost moves into the draw call and it is 10x worse.
* Ring buffers for streamed vertices: Piglet allocates buffer memory in 10 MiB chunks out of a pool the game already
  fills to within 6 MiB, so a 4 MiB ring cannot be allocated.
* The GPU is not the limit at 1080p in the heavy scenes: halving the viewport, removing blending and removing filtering
  did not change the frame time. The cost was CPU submission, about 12 microseconds a draw.

## Known limits

* One console tested. 720p and 1080p both work; the 1080p picture is the console's own, the 720p is scaled by it.
* No depth read-back on ES 2: picks that rely on it (some lens and occlusion effects) report "no hit".
* No Dark Chronicle (Vu_prog0) meshes: the game this port targets does not use them.
* Piglet's memory pool is small (236 MiB configured). The log reports `Reached user-configured max FMEM limit` and a
  failed texture allocation if a scene needs more than it has.
* The frame-rate overlay forces the render thread to wait every tick; leave it off for play.

## Credits

Chronicle (TheMoonPeople) is the decompilation this builds on; the PS4 platform layer is adapted from alechurri's
Ship of Harkinian PS4 port (MIT, see `LICENSE-ps4-port`). OpenOrbis provides the toolchain and GoldHEN the loader; Piglet is Sony's.
No game data or Sony modules are included.
