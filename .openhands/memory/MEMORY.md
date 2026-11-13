# Project memory (Asuka)

Repo: `M5Devs/Asuka` — "Retro Vintage PC Runner & Modular Environment".
Cross-platform, modular vintage PC runner. **Modern C++17** + CMake ≥ 3.16,
GPLv3. `LICENSE` pre-existing; `README.md` rewritten in Task 1.

## Architecture (Task 1 — foundation, verified)
- Plugin model is **Libretro-inspired but object-oriented**: the *host owns the
  main loop* and *pulls* state (`get_framebuffer`, `get_audio_samples`) after
  calling `step_frame()`. Cores never drive the loop.
- `include/asuka/IEngineCore.hpp` — pure-virtual ABI. Framebuffer is
  **XRGB8888** (`uint32_t`); audio is **interleaved stereo `int16_t`**,
  `sample_count` is *per channel*.
- Factory symbols a plugin must export (resolved by name): `create_engine`,
  `destroy_engine` (see `ASUKA_*_SYMBOL` macros). Typedefs `CreateEngineFunc` /
  `DestroyEngineFunc` live at *global* scope (not inside `namespace asuka`).
- `PluginLoader` wraps `dlopen/dlsym/dlclose` (POSIX) and
  `LoadLibrary/GetProcAddress/FreeLibrary` (Win32), gated on
  `ASUKA_PLATFORM_WINDOWS` (`include/asuka/Export.hpp`). Move-only.
  `EngineInstance` is the RAII owner: **must be reset/destroyed before the
  loader is unloaded**.
- `PluginLoader::library_name()` returns `lib<base>.so` / `lib<base>.dylib` /
  `<base>.dll` (no `lib` prefix on Windows).

## Build & layout
- Exact commands: `cmake -B build && cmake --build build`. Default build type
  is **Release** when unset.
- Outputs: runner at `build/bin/asuka_runner`; dummy plugin at
  `build/plugins/dummy/libasuka_dummy_engine.so` (also the runner's default
  lookup path, overridable via `ASUKA_PLUGIN_PATH` or argv[1]).
- Warnings are strict via an `asuka_warnings` INTERFACE target
  (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow ...`).
  Avoid gratuitous `int`→`size_t` mixing in index math (cost a warning once).
- Plugin builds with `CXX_VISIBILITY_PRESET hidden`; only the two factories and
  libc remain in the dynamic symbol table. Verify with
  `nm -DC <lib> | grep ' T '`.
- `ctest --test-dir build` runs the `asuka_smoke` test (runner + plugin).
- Toolchain present in this container: cmake 3.31, g++ 14.2, 4 cores.

## Verification that passed
- Clean `cmake -B build && cmake --build build`: **0 warnings**.
- `asuka_runner`: 60 frames, 320x240 XRGB8888, centre pixel animates
  (`0xFF7F11ED` → `0xFF127DEF`), 800 audio samples/ch, clean teardown, "OK".
- ASan+UBSan+LSan build (`-fsanitize=address,undefined`) → **no leaks/errors**.
- Error paths return exit 1 with a readable message: missing file, and a real
  library lacking the factory symbols (tested against `libm.so.6`).

## Notes / gaps for next tasks
- `send_input` is stored but unused by the dummy (key codes are core-defined).
- No video/audio *output* backends yet — the runner only prints state.
- Pre-existing memory content described an unrelated SAGE project; replaced here.

## Task 2 — FM Towns plugin (Geartowns), verified
- `plugins/fmtowns` wraps **Geartowns** (`drhelius/Geartowns`, GPLv3) as the
  first real engine. Geartowns is a **git submodule** at
  `plugins/fmtowns/vendor/Geartowns` (pinned `9f1a879` = 1.0.6-38). The
  top-level CMake **skips** the plugin with a warning when the submodule is
  uninitialised, so a plain checkout still configures.
- Submodule caveat: after `git submodule add`, the first push/ commit added
  `.gitmodules` + the gitlink; the deps are vendored *in-tree* (no nested
  submodules), so `git submodule update --init` alone is enough downstream.
- **Critical CMake gotcha:** the top-level `project()` originally declared
  `LANGUAGES CXX` only, so CMake **silently dropped every `.c` source** — the C
  dependency objects were never built and the plugin linked with undefined
  `chd_*`/`mz_*`/`ov_*` symbols that only failed at `dlopen` time. Fixed by
  `LANGUAGES C CXX`. If a vendor is C, enable C.
- C++-only warning flags on the shared `asuka_warnings` INTERFACE leak into C
  compiles ("`-Wnon-virtual-dtor` is valid for C++ but not C"). Fixed with
  `$<$<COMPILE_LANGUAGE:CXX>:...>` generator expressions.
- Geartowns core needs one host-provided global: **`g_mcp_stdio_mode`** (declared
  in `src/common/log.h`, defined by the desktop app). `gt_compat.cpp` defines it
  `false` for headless embedding — without it the plugin has an undefined symbol.
- Build scope: only the emulation core + data deps (`libchdr`, `lzma`,
  `minivorbis`, `miniz`, `bochs_fpu`) are compiled. The SDL3/OpenGL/imgui/glad
  desktop shell is **not** built, so the plugin has no GUI deps. Defined:
  `GT_DISABLE_DYNAREC` (interpreter, no Lightning dep), `Z7_ST`,
  `ZSTD_DISABLE_ASM`, `EMULATOR_BUILD="asuka"`. Vendor headers are `SYSTEM`
  includes and their sources get `-w`.
- Geartowns API used: `Init()`, `LoadBios(dir)`, `SetMachineSettings({model,
  cpu_mhz, ram_mb, fpu, speed_mode}; -1 = GT_MACHINE_AUTO)`, `PowerOn()`,
  `LoadMedia(path)`, `LatchAutoMedia()`, `ResetMedia()`,
  `RunToFrame(u8* xrgb, s16* pcm, int* sample_count, bool render=true)`,
  `GetRuntimeInfo(GT_Runtime_Info&)` (out-param, **not** const-ref),
  `KeyPressed/KeyReleased(GT_Keys)`, `PowerOff()`.
- Frame buffer is at most **1024x768** (`GT_MAX_FRAME_BUFFER_WIDTH/HEIGHT`) with
  a row stride of `screen_width` (use `screen_width/height`, *not*
  `display_width`, for the buffer layout). Audio `sample_count` from
  `RunToFrame` is **interleaved s16 values**, so per-channel = count/2.
- Firmware (`FMT_SYS.ROM`, `FMT_DOS.ROM`, `FMT_FNT.ROM`, `FMT_DIC.ROM`, …) is
  **CRC-matched and copyright**, so it is absent: `init()` fails cleanly
  ("failed to load FM Towns BIOS" / "No firmware found"). Emulation itself is
  therefore unverified beyond the load + error path.
- Runner now has `--plugin --system --bios --media --frames --dump-frame`
  (PPM P6 dump verified). `ctest` runs `asuka_smoke` + `asuka_fmtowns_loads`
  (the latter `WILL_FAIL`, asserting the no-BIOS error path).
- Verified: clean rebuild 0 warnings, plugin exports exactly
  `create_engine`/`destroy_engine`, no undefined non-libc symbols, ASan/LSan
  clean for both plugins.

## Git / push (Task 2 policy)
- Policy: **push straight to `origin/main`, no PR.** The embedded credential in
  the `origin` URL is stale (prompts for a password); push with
  `git push "https://${GITHUB_TOKEN}@github.com/M5Devs/Asuka.git" main`.
- `origin` = `https://github.com/M5Devs/Asuka.git`; branch `main`. Commits:
  `04eb868` (initial, grafted), `ab9c6d0` (foundation), `bc3514a` (memory),
  `b7cb447` (FM Towns plugin), `ae69347` (memory), `63df591` (audio fix + boot
  verification).

## Task 3 — live boot verification (Geartowns), verified end-to-end
- **Audio buffer overflow (real bug the dummy never hit).** Our destination PCM
  buffer was sized from a per-frame estimate (`2*(rate/fps+512)` ≈ 2494 shorts).
  Geartowns accumulates a whole frame into a buffer capped at
  **`GT_AUDIO_BUFFER_SIZE` (8192, `src/common/defines.h`)** and `Audio::EndFrame`
  copies the *entire* `m_buffer_index` out, so the copy overran us whenever the
  audio clock ran ahead of the video clock → heap corruption → `free(): invalid
  pointer` **at teardown** (after all frames had already run). Fix: size the
  buffer to `GT_AUDIO_BUFFER_SIZE`. Found with ASan (heap-buffer-overflow in
  `Audio::EndFrame`, audio.cpp:696). Symptom lesson: a crash *after* "OK" frames
  is still a real bug; don't dismiss it as a shutdown quirk.
- **Test assets** (copyrighted → git-ignored `test_assets/`, scripts added):
  - BIOS: `Abdess/retrobios`, path **`bios/Fujitsu/FM Towns/`** (sparse clone
    works: `git clone --depth 1 --filter=blob:none --sparse` then
    `git sparse-checkout set --no-cone '/bios/Fujitsu/FM Towns/*'`). Note the
    top-level dir is `bios/`, not `fmtowns/` or `fujitsu/`.
  - Media: archive.org item **`fmtownscdcollection`**, file
    `FM-Towns CD/Afterburner (1987)(Sega)(Jp-En).zip` (204 MB) → CUE + BIN
    (230 MB, Mode1/2352 data track + 7 audio tracks).
  - Helpers: `scripts/fetch_test_assets.sh` (fetch) and
    `scripts/verify_fmtowns_boot.sh` (boot + assert non-blank frame).
- **ROM set identity matters.** The retrobios `FMT_SYS.ROM` (crc32 `afe4ebcf`)
  is the **`fmtownsftv` / "V10"** set, *not* the Model 1/2 `fmtowns` set
  (`k_firmware_sets` in `src/media/firmware.h`). Geartowns still auto-selects a
  working model from the loaded set, so boot succeeds, but the machine is a
  "FM Towns II Fresh TV". `FMT_DOS.ROM` is shared across sets.
- **Boot progression with a real BIOS + After Burner** (frame → mode):
  ~0-500 → 640×480 (POST), ~500-1400 → 640×400 (boot menu / text, 8 colours),
  ~1500+ → **320×240** (game's native mode, 733 distinct colours, ~6% of pixels
  change per 60 frames → live animation; the rendered title/shape is visible).
  The resolution change 640×480→640×400→320×240 is the machine programming its
  CRTC, i.e. real emulation, not a static buffer.
- Verified: clean release rebuild 0 warnings, `ctest` 2/2, ASan/LSan clean over
  a 600-frame real boot (exit 0, no leaks), verification script PASS
  (`733 colours`, `72% non-black ink`).

## Task 4 — boot recording (MP4/GIF), verified
- **Runner gained `--dump-frames <dir>`** (`src/main.cpp`) → writes every frame
  as `<dir>/frame_%04d.ppm`. Needs `<filesystem>` (GCC 14, C++17, no `-lstdc++fs`).
- **Mode changes mid-recording**: 640×480 (frames 0-7) → 640×400 (8-1124) →
  320×240 (1125+). ffmpeg's image2 demuxer needs a *constant* frame size, so
  `scripts/normalise_frames.py` letterboxes every frame onto one 640×480 canvas
  at the **largest integer scale that fits** (per-frame, NOT a global scale —
  640×400 would blow up to 1280×800). 320×240 → exactly 2×, fills the canvas.
- **Long static stretch**: frames **200-1120 are byte-identical** (the machine
  idles on the boot screen ~15 s). `record_fmtowns_boot.sh` cuts it with
  `plan = [(0,200),(1100,1799)]`; the join is between two identical frames so
  the cut is invisible. Result: 901 frames = 15.0 s @ 60 fps.
- **Timeline of the final clip** (t in s): 0-1 blank → 1-3.5 boot menu appears
  (8.8% ink, 640×400 letterboxed) → 3.5-5 boot menu static → 5-6 mode switch →
  6-10 blank 320×240 → 10-13 title screen (72% ink, ~20k colours) → 13-15 fade.
- **Encoding commands that work**:
  - MP4: `ffmpeg -framerate 60 -i .../frame_%04d.ppm -c:v libx264 -pix_fmt
    yuv420p -preset slow -crf 18 -movflags +faststart` → 15 s, ~520 KB.
  - GIF: `-vf "fps=20,split[a][b];[a]palettegen=max_colors=256:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle" -loop 0`
    → 300 frames, 640×480, ~1.1 MB (two-pass palette; good size/quality).
- **Committed demo**: `docs/media/afterburner_boot.{mp4,gif}` embedded in the
  README (`docs/media/` is *not* git-ignored; `test_assets/` is).
- **ffmpeg was missing** → `sudo apt-get install -y ffmpeg` (passwordless sudo
  works; plain apt-get fails, the lists lock is root-owned). Installed 7.1.5.
  Resets with the container, like other tools.
- Full pipeline: `scripts/record_fmtowns_boot.sh` (runs emulator → builds
  sequence → normalises → encodes → deletes raw frames), ~10 s end to end.
  Rebuild of runner + `ctest` still 2/2 after the `--dump-frames` change.
- Commits: `c1acb78` (recording pipeline + demo clip). Remote head `c1acb78`.
