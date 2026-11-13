# Asuka

A cross-platform, modular **vintage PC runner**: a host application that loads
emulation cores as dynamically-linked plugins behind a single clean C++
interface.

Licensed under the [GNU General Public License v3.0](LICENSE).

## Status

- **Task 1 — foundation.** The skeleton, the plugin ABI (`IEngineCore`), the
  cross-platform dynamic loader, a reference *dummy* plugin and a CLI test
  runner.
- **Task 2 — first real engine.** The **FM Towns / Marty** core (Geartowns) is
  integrated as `plugins/fmtowns`.
- **Task 3 — capture and analysis.** Headless boot recording (video + audio) and
  an audio analysis of the FM Towns boot.
- **Task 4 — interactive runner.** `asuka_play`, an SDL2 desktop runner with a
  window, live audio playback and keyboard/gamepad input, so games can be played
  in real time.
- **Task 5 — the browser.** The FM Towns core builds to WebAssembly with
  Emscripten (`scripts/build_web.sh`), and a self-contained web shell
  (`web/index.html`) runs it on a canvas with Web Audio. Ready to deploy to
  Cloudflare Pages.
- **Task 8 — the second machine.** The **NEC PC-9801 / PC-9821** core (NP2kai,
  from WebNP2) is integrated as `plugins/pc98`, alongside the FM Towns core in
  the same WebAssembly build. The web shell adds a floating **🎮 ⇄ ⌨️** switch
  between a virtual gamepad and a virtual PC-98 keyboard.

## Architecture

The design follows the Libretro philosophy: the **host owns the main loop** and
the **core exposes state**. A core never drives the loop itself; the host calls
`step_frame()` and then *pulls* the framebuffer and audio buffers. Two hosts
share that contract: the headless `asuka_runner` (frame stepping, capture) and
the interactive `asuka_play` (window, sound, input).

```
        ┌──────────────────────────┐
        │  asuka_runner  asuka_play │   host main loop
        │ (src/main.cpp) (interactive)│
        └───────────┬──────────────┘
                    │ uses
        ┌───────────▼──────────────┐
        │      PluginLoader        │   dlopen / LoadLibrary
        │  (src/core/PluginLoader) │   dlsym  / GetProcAddress
        └───────────┬──────────────┘
                    │ resolves create_engine / destroy_engine
        ┌───────────▼──────────────┐
        │      IEngineCore         │   pure-virtual plugin ABI
        │ (include/asuka/*.hpp)    │
        └───────────┬──────────────┘
                    │ implemented by
        ┌───────────▼──────────────┐
        │  libasuka_dummy_engine   │   reference core
        │  (plugins/dummy/)        │
        └──────────────────────────┘
```

### Plugin ABI

Every core implements `asuka::IEngineCore`:

| Method | Purpose |
| --- | --- |
| `init(system_path, bios_path)` | one-time setup |
| `load_media(media_path)` | load a ROM/disk image |
| `step_frame()` | advance exactly one video frame |
| `get_framebuffer(w, h)` | pull current frame, XRGB8888 `uint32_t*` |
| `get_audio_samples(n)` | pull interleaved stereo `int16_t` PCM, `n` per channel |
| `send_input(key_code, pressed)` | feed a host key event (see `asuka::Key`) |
| `reset()` | soft reset |
| `shutdown()` | release resources |

`send_input` takes a `uint32_t` so a core may define its own codes, but a
generic host cannot know them. The shared, deliberately small `asuka::Key` enum
(`include/asuka/Input.hpp`) is the contract: the host translates physical input
to those keys and each core maps them to its own (the object-oriented analogue
of Libretro's `RETRO_DEVICE_ID_JOYPAD_*`). A core ignores keys it has no use
for.

A plugin shared library exports exactly two C symbols:

```c
asuka::IEngineCore* create_engine(void);
void                destroy_engine(asuka::IEngineCore*);
```

`PluginLoader` wraps `dlopen`/`dlsym`/`dlclose` (Linux/macOS) and
`LoadLibrary`/`GetProcAddress`/`FreeLibrary` (Windows) and resolves that pair.
`EngineInstance` is an RAII owner that guarantees the engine is shut down and
destroyed before its library is unloaded.

## Layout

```
include/asuka/
  IEngineCore.hpp     unified plugin interface + factory typedefs
  Input.hpp           host-level input keys (asuka::Key)
  Export.hpp          platform export/visibility macros
  PluginLoader.hpp    loader + RAII EngineInstance
src/
  core/PluginLoader.cpp
  main.cpp            headless CLI runner (asuka_runner)
  interactive/
    main_sdl.cpp      interactive SDL2 runner (asuka_play)
  web/
    main_web.cpp      Emscripten entry + JS-facing API (asuka_web)
plugins/dummy/
  DummyEngine.{hpp,cpp}   mock core: colour-cycle video + sine audio
  dummy_plugin.cpp        exported create_engine/destroy_engine
plugins/fmtowns/
  FMTownsEngine.{hpp,cpp} Geartowns-backed FM Towns / Marty core
  fmtowns_plugin.cpp      exported create_engine/destroy_engine
  fmtowns_plugin.hpp      factory declarations for the static (web) link
  gt_compat.cpp           provides symbols Geartowns' desktop app supplies
  vendor/Geartowns/       git submodule (the emulator)
  CMakeLists.txt
plugins/pc98/
  PC98Engine.{hpp,cpp}    NP2kai-backed PC-9801 / PC-9821 core
  pc98_plugin.cpp         create_pc98_engine + (desktop) create_engine aliases
  pc98_plugin.hpp         factory declarations for the static (web) link
  vendor/NP2kai/          git submodule (NP2kai, Libretro frontend)
  CMakeLists.txt
web/
  index.html          the browser shell (canvas, touch pad, PC-98 keyboard)
  _headers            COOP/COEP + MIME headers for Cloudflare Pages
scripts/
  build_web.sh        Emscripten build -> web/dist
  serve_web.py        local server with the isolation headers
wrangler.toml         Cloudflare Pages project config
CMakeLists.txt
```

## FM Towns plugin (Geartowns)

`plugins/fmtowns` adapts [Geartowns](https://github.com/drhelius/Geartowns)
(GPLv3, an FM Towns / Marty emulator) to `IEngineCore`. Only the emulation core
and its data dependencies are compiled — the SDL3/OpenGL/imgui desktop shell is
left out, so the plugin has no GUI dependencies.

The plugin is added as a git submodule, so fetch it first:

```sh
git submodule update --init --recursive
```

If the submodule is missing, configuration still succeeds and the runner builds;
CMake prints a warning and simply skips the plugin.

### BIOS / firmware

FM Towns needs the machine ROM set (`FMT_SYS.ROM`, `FMT_DOS.ROM`, `FMT_FNT.ROM`,
`FMT_DIC.ROM`, plus `FMT_F20.ROM` for some models) pointed at with `--bios`.
These are copyrighted and are **not** distributed here; without them `init()`
fails cleanly with a message. Media images (CHD, CUE/BIN, ISO, floppy) load
after a successful init.

## PC-98 plugin (NP2kai)

`plugins/pc98` adapts [NP2kai](https://github.com/AZO234/NP2kai) — the Neko
Project II fork that [WebNP2](https://github.com/uraraworks/WebNP2) uses — to
`IEngineCore`. Only the emulation core and its Libretro shim are compiled; the
Libretro frontend and the menu shell are left out, so the plugin has no GUI or
SDL dependency of its own.

Fetch the submodule the same way as FM Towns:

```sh
git submodule update --init --recursive
```

If it is missing, configuration still succeeds and CMake skips the plugin.

Unlike the FM Towns core, the PC-98 core boots from its **built-in BIOS I/O
emulation**, so it renders and produces sound with no external ROM at all
(`BIOS_IO_EMULATION`). Point `--bios` at a PC-98 ROM set for a full boot. The
`PC98Engine` implements the full `IEngineCore` surface:

| Method | Behaviour |
| --- | --- |
| `init(system, bios)` | boot the NP2kai core; `bios` selects the ROM directory |
| `load_media(path)` | mount `.d88` / `.fdi` / `.hdi` / `.nhd` / `.iso` images |
| `step_frame()` | run one frame (`retro_run` → `np2_main`) |
| `get_framebuffer()` | 640×400 XRGB8888, converted from the core's output |
| `get_audio_samples()` | 44.1 kHz stereo OPNA / FM samples |
| `send_input(key, pressed)` | map `asuka::Key` to PC-98 scan codes |
| `send_scancode(code, pressed)` | inject a raw PC-98 scan code directly |
| `save_snapshot()` / `load_snapshot()` | NP2kai state + BMP thumbnail |

The raw-scancode entry point is what the virtual keyboard uses for keys the
small `asuka::Key` set cannot express (`HELP`, `STOP`, `ROLL UP/DOWN`, `F1`-`F5`).
Scan codes follow the PC-98 hardware layout (see `keystat.h` in the submodule),
not PC/AT: for example `HELP` is `0x3f`, `STOP` is `0x60`, `RETURN` is `0x1c`.

## Build

Requires CMake ≥ 3.16 and a C++17 compiler.

```sh
git submodule update --init --recursive   # fetch Geartowns (for the FM Towns plugin)
cmake -B build
cmake --build build
```

The interactive runner (`asuka_play`) additionally needs **SDL2**. CMake finds
it with `find_package(SDL2)`; if SDL2 is missing the headless `asuka_runner`
still builds and CMake prints a note. To skip it explicitly:

```sh
cmake -B build -DASUKA_BUILD_SDL=OFF
```

On Debian/Ubuntu install `libsdl2-dev`.

## Run

```sh
# default plugin (the dummy core)
./build/bin/asuka_runner

# point it explicitly at a plugin
./build/bin/asuka_runner --plugin build/plugins/fmtowns/libasuka_fmtowns.so \
                         --bios /path/to/fmtowns-roms

# more options
./build/bin/asuka_runner --help
```

| Option | Meaning |
| --- | --- |
| `--plugin <path>` | engine plugin shared library to load |
| `--system <dir>` | engine system/asset directory (first `init()` argument) |
| `--bios <dir>` | directory with BIOS/ROM files (second `init()` argument) |
| `--media <path>` | media image (CHD, CUE/BIN, ISO, floppy) |
| `--frames <N>` | frames to run (default 60) |
| `--dump-frame <path>` | write the final frame to a binary PPM (P6) |
| `--press <frame>:<key>` | press a host key (`asuka::Key`) at that frame |
| `--press-scancode <frame>:<code>` | press a core-native scancode at that frame |
| `--save-snapshot <path>` | save a machine snapshot after the last frame |
| `--load-snapshot <path>` | load a machine snapshot before the first frame |

A plugin path may also be given positionally, or via `ASUKA_PLUGIN_PATH`.

The runner loads the plugin, initialises it, runs the requested frames while
reporting the framebuffer size and audio sample count, then tears everything
down and unloads the library.

### Save states and thumbnails

An engine that supports snapshots saves the whole machine — CPU, RAM, video,
audio and media position — to a single file, and writes a matching preview
beside it. The FM Towns plugin writes a 24-bit BMP next to the state file (same
name, `.bmp` extension) so a front-end can show a thumbnail without decoding the
state. The BMP is written from the current framebuffer, so save after the frame
you want to preview. Snapshots are validated end to end by
`tests/snapshot_test.sh`, which also checks that resuming from a state
reproduces the exact frame a straight run would have produced.

## Interactive runner (`asuka_play`)

`asuka_play` is the real-time counterpart to `asuka_runner`: it opens a window,
plays the engine's audio through SDL2, and feeds keyboard/gamepad input, so a
game can actually be played.

```sh
./build/bin/asuka_play \
    --plugin build/plugins/fmtowns/libasuka_fmtowns.so \
    --bios /path/to/fmtowns-roms \
    --media "/path/to/Afterburner (1987)(Sega)(Jp-En).cue"
```

| Option | Meaning |
| --- | --- |
| `--plugin <path>` | engine plugin shared library to load (required) |
| `--system <dir>` | engine system/asset directory (first `init()` argument) |
| `--bios <dir>` | directory with BIOS/ROM files (second `init()` argument) |
| `--media <path>` | media image (CHD, CUE/BIN, ISO, floppy) |
| `--frames <N>` | stop after `N` frames (default: run until quit) |
| `--width <W>` / `--height <H>` | initial window size (default 640×480) |
| `--vsync` | additionally sync presentation to the display |
| `--no-integer-scale` | allow fractional scaling instead of integers |
| `--headless` | no window; still runs the loop and audio |
| `--no-audio` | do not open an audio device |
| `--snapshot <path>` | state file for the F5/F8 snapshot keys (default `<media>.state`) |

Controls: arrow keys are the d-pad, **Enter** is Start, **Space/Z/X/C** are the
fire buttons, **R** resets, **F11** toggles fullscreen, **F5** saves a snapshot,
**F8** loads it, and **Esc** quits. A connected game controller is picked up
automatically (d-pad, left stick, face buttons, shoulders).

The host pulls one frame at a time and paces the loop to the audio clock: it
queues each frame's PCM and sleeps only while the device buffer is fuller than
a small target latency. That keeps sound continuous while the video tracks the
machine's true rate (FM Towns video runs at ~58.7 Hz, and the engine emits a
variable number of samples per frame). With `--no-audio` the loop falls back to
a nominal 60 Hz. The framebuffer is uploaded to a streaming texture and drawn
letterboxed with integer scaling and nearest-neighbour filtering, so the pixel
art stays crisp; the texture is recreated when the machine changes video mode
mid-session.

## Web (WebAssembly + Cloudflare Pages)

Both cores — FM Towns and PC-98 — build into a single WebAssembly module with
Emscripten, and `web/index.html` is a self-contained shell that runs either one
on a `<canvas>` with Web Audio, keyboard/gamepad input and an on-screen touch
pad. There is no `dlopen` on the web: the engines are linked straight into the
module and the shell calls a small C API (`asuka_web_init`, `asuka_web_start`,
…). The engine is chosen at runtime:

```js
// third argument to asuka_web_init; "fmtowns" is the default.
asuka_web_init("/bios", "/media/disc.iso", "pc98");
```

The shell takes the machine from `?machine=pc98` in the URL, falling back to the
`machine` field in `assets/manifest.json`, then to FM Towns.

### Dual touch input (gamepad ⇄ PC-98 keyboard)

A floating **🎮 ⇄ ⌨️** button (top-left, shown on touch devices and on PC-98)
swaps the on-screen controls between two modes:

- **Gamepad** — the D-pad plus A/B, for pad-driven arcade games.
- **PC-98 keyboard** — a compact virtual keyboard: the standard rows
  (Q-P, A-L, Z-M, digits, Space, Return) plus the PC-98 utility keys `HELP`,
  `STOP`, `ROLL UP`, `ROLL DOWN` and `F1`-`F5`.

Gamepad keys go through `asuka_web_key` (the `asuka::Key` set); keyboard keys go
through `asuka_web_scancode`, which injects the raw PC-98 scan code so the
utility keys work. A physical keyboard also types into the PC-98 when the
machine is PC-98. Held keys are released on every mode switch, so nothing sticks.

### Prerequisites

The [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
source ~/emsdk/emsdk_env.sh
```

### Build

```sh
scripts/build_web.sh              # -> web/dist/{index.html,_headers,asuka_web.js,asuka_web.wasm}
python3 scripts/serve_web.py      # http://127.0.0.1:8000/
```

Open the page, drag the BIOS ROMs and a disc image onto it (or use the pickers),
choose the disc and press **Boot**. Files are stored in the browser's IndexedDB
and never uploaded. `serve_web.py` sends the two headers the build needs:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Those make the page *cross-origin isolated*, which is what enables
`SharedArrayBuffer` and therefore WASM threads — the Geartowns CD reader uses a
read-ahead thread, so a plain static server without them will not work.

### Deploy to Cloudflare Pages

The build output is a plain static site, so any static host works. For Cloudflare
Pages there are two one-step paths:

- **Dashboard:** Workers & Pages → Create → Pages → Connect to Git, pick
  `M5Devs/Asuka`, set the build command to `bash scripts/build_web.sh` and the
  output directory to `web/dist`. `web/_headers` is applied automatically.
- **CLI:** `wrangler pages deploy web/dist --project-name asuka`
  (config in `wrangler.toml`).

`.github/workflows/web.yml` does the same on every push to `main`: it builds the
bundle, uploads it as an artefact, and deploys to Pages when
`CLOUDFLARE_API_TOKEN` / `CLOUDFLARE_ACCOUNT_ID` repository secrets are set.

## Test

```sh
ctest --test-dir build --output-on-failure
```

The suite covers the dummy plugin through the headless runner (`asuka_smoke`),
the audio capture path (`asuka_audio_capture`), the FM Towns plugin load
(`asuka_fmtowns_loads`), the FM Towns snapshot round-trip (`asuka_snapshot`), and
the PC-98 engine (`asuka_pc98`: boot, render, audio, snapshot round-trip and raw
scancode input). When SDL2 is present, the interactive runner's whole loop is
also driven headlessly with SDL's dummy video/audio backends
(`asuka_play_headless`).

## Live boot verification

Beyond the unit tests, the FM Towns plugin can be verified end-to-end against a
real BIOS and disc image. The assets are copyrighted, so they are **not**
tracked (everything lives under the git-ignored `test_assets/`).

```sh
scripts/fetch_test_assets.sh      # BIOS from retrobios + After Burner disc
scripts/verify_fmtowns_boot.sh    # boots it and asserts the frame has content
scripts/record_fmtowns_boot.sh    # records the boot to MP4 + GIF (with audio)
scripts/analyze_fmtowns_audio.sh  # captures audio and maps where sound appears
```

`fetch_test_assets.sh` pulls the Fujitsu FM Towns ROM set from
`Abdess/retrobios` and the After Burner (1987, Sega) CUE/BIN from the Internet
Archive's `fmtownscdcollection` item. `verify_fmtowns_boot.sh` then boots the
disc, dumps a frame once the machine has switched to its graphics mode, and
fails unless the frame has both enough distinct colours and enough non-black
coverage to be real video rather than a blank screen.

With a genuine ROM set the machine progresses through POST (640×480), the
boot menu (640×400, text mode), and finally the game's native 320×240 mode,
which renders the After Burner title with hundreds of distinct colours and
changes frame to frame.

### Demo

![After Burner boot on FM Towns](docs/media/afterburner_boot.gif)

The 15-second clip above (also available as
[`afterburner_boot.mp4`](docs/media/afterburner_boot.mp4)) shows the boot menu
appearing, the video mode switching to 320×240, and the After Burner title
rendering and animating. `record_fmtowns_boot.sh` produces it: it runs the
emulator with every frame dumped, normalises the frames onto one canvas (the
machine changes video mode mid-boot, and ffmpeg needs a constant frame size),
cuts the long stretch where the machine sits on a byte-identical boot screen,
and encodes H.264 plus an optimised GIF. The MP4 also carries a stereo audio
track, spliced to the same frame cut.

### Audio

The runner captures the engine's audio with `--dump-audio <path>` (16-bit
stereo WAV at 44100 Hz) and, with `--dump-audio-frames <path>`, a sidecar that
records how many samples each frame produced. The engine emits a variable
number of samples per frame, so that sidecar is what lets a recording that cuts
frames splice the audio to match exactly.

`analyze_fmtowns_audio.sh` records the audio and prints a per-second loudness
and tonality map. On After Burner the map shows that the boot menu and the
title screen are **silent**, and that sound only starts in the attract mode
that follows: the guest never issues a CD-DA play command during boot or the
title, and only programs the FM chip later. The audio path itself is healthy —
the same capture records that later FM music and sound effects — so the silent
intro is authentic behaviour, not a broken mixer.
