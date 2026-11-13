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

## Architecture

The design follows the Libretro philosophy: the **host owns the main loop** and
the **core exposes state**. A core never drives the loop itself; the host calls
`step_frame()` and then *pulls* the framebuffer and audio buffers.

```
        ┌──────────────────────────┐
        │        asuka_runner      │   host main loop
        │  (src/main.cpp)          │
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
| `send_input(key_code, pressed)` | feed an input event |
| `reset()` | soft reset |
| `shutdown()` | release resources |

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
  Export.hpp          platform export/visibility macros
  PluginLoader.hpp    loader + RAII EngineInstance
src/
  core/PluginLoader.cpp
  main.cpp            CLI test runner
plugins/dummy/
  DummyEngine.{hpp,cpp}   mock core: colour-cycle video + sine audio
  dummy_plugin.cpp        exported create_engine/destroy_engine
plugins/fmtowns/
  FMTownsEngine.{hpp,cpp} Geartowns-backed FM Towns / Marty core
  fmtowns_plugin.cpp      exported create_engine/destroy_engine
  gt_compat.cpp           provides symbols Geartowns' desktop app supplies
  vendor/Geartowns/       git submodule (the emulator)
  CMakeLists.txt
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

## Build

Requires CMake ≥ 3.16 and a C++17 compiler.

```sh
git submodule update --init --recursive   # fetch Geartowns (for the FM Towns plugin)
cmake -B build
cmake --build build
```

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

A plugin path may also be given positionally, or via `ASUKA_PLUGIN_PATH`.

The runner loads the plugin, initialises it, runs the requested frames while
reporting the framebuffer size and audio sample count, then tears everything
down and unloads the library.

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Live boot verification

Beyond the unit tests, the FM Towns plugin can be verified end-to-end against a
real BIOS and disc image. The assets are copyrighted, so they are **not**
tracked (everything lives under the git-ignored `test_assets/`).

```sh
scripts/fetch_test_assets.sh      # BIOS from retrobios + After Burner disc
scripts/verify_fmtowns_boot.sh    # boots it and asserts the frame has content
scripts/record_fmtowns_boot.sh    # records the boot to MP4 + GIF
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
and encodes H.264 plus an optimised GIF.
