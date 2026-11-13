# Project memory (Asuka)

Repo: `M5Devs/Asuka` — "Retro Vintage PC Runner & Modular Environment".
Cross-platform, modular vintage PC runner. **C++17** + CMake ≥ 3.16, GPLv3.
Build: `cmake -B build && cmake --build build` (default type Release).

## Architecture (verified)
- Plugin model is **Libretro-inspired but object-oriented**: the *host owns the
  main loop* and *pulls* state (`get_framebuffer`, `get_audio_samples`) after
  `step_frame()`. Cores never drive the loop. Two hosts: headless
  `asuka_runner` (`src/main.cpp`) and interactive `asuka_play`
  (`src/interactive/main_sdl.cpp`).
- `include/asuka/IEngineCore.hpp` — pure-virtual ABI. Framebuffer **XRGB8888**
  (`uint32_t`); audio **interleaved stereo `int16_t`**, `sample_count` is *per
  channel*. Factory symbols resolved by name: `create_engine`, `destroy_engine`
  (typedefs `CreateEngineFunc`/`DestroyEngineFunc` at *global* scope).
- `include/asuka/Input.hpp` — host-level `asuka::Key` enum (d-pad, Start,
  Select, A/B/X/Y/L/R). `send_input` takes a `uint32_t`; the *host* translates
  physical input to `asuka::Key` and *each core* maps those to its own codes
  (Libretro `RETRO_DEVICE_ID_JOYPAD_*` analogue). Cores ignore unknown keys.
- `PluginLoader` wraps `dlopen/dlsym/dlclose` (POSIX) and
  `LoadLibrary/GetProcAddress/FreeLibrary` (Win32), gated on
  `ASUKA_PLATFORM_WINDOWS` (`include/asuka/Export.hpp`). `EngineInstance` is the
  RAII owner: **`engine.reset()` calls `shutdown()` then `destroy_engine()`**,
  and must happen **before `loader.unload()`**.
- Outputs: `build/bin/asuka_runner`, `build/bin/asuka_play` (SDL2, optional);
  dummy plugin `build/plugins/dummy/libasuka_dummy_engine.so` (runner's default
  lookup, overridable via `ASUKA_PLUGIN_PATH` / argv[1]).

## Git / push policy
- **Push straight to `origin/main`, no PR.** The credential embedded in the
  `origin` URL is stale (prompts for a password); push with
  `git push "https://${GITHUB_TOKEN}@github.com/M5Devs/Asuka.git" main`.
- Commits (task order): `04eb868`→`ab9c6d0`(foundation)→`b7cb447`(FM Towns
  plugin)→`63df591`(audio fix)→`c1acb78`(recording)→`4ce8847`(boot audio).

## FM Towns plugin (Geartowns, `plugins/fmtowns`)
- Only the emulation core + data deps are built; the SDL3/OpenGL/imgui/glad
  desktop shell is excluded, so the plugin has **no GUI deps**. Submodule
  `vendor/Geartowns`. Uses `GT_DISABLE_DYNAREC` (interpreter, deterministic).
- `GT_MAX_FRAME_BUFFER_WIDTH/HEIGHT` = 1024×768 (our framebuffer alloc).
  Audio: Geartowns accumulates one frame into a buffer capped at
  **`GT_AUDIO_BUFFER_SIZE` (8192)** and copies the whole `m_buffer_index` out,
  so size our destination to `GT_AUDIO_BUFFER_SIZE` — a per-frame estimate
  overflows (was a heap-corruption bug).
- **Input mapping (`ToGtKey`)**: d-pad→cursor keys, Start→`GT_KEY_RETURN`,
  Select/A→`GT_KEY_SPACE`, B/L→`Z`, X/R→`X`, Y→`C`. Verified end-to-end by
  instrumenting `Keyboard::KeyPressed` (start=0x1D, a=0x35, left=0x4F, b=0x2A,
  x=0x2B, y=0x2C, all valid).
- Boot progression (real BIOS + After Burner): ~0-500 640×480 (POST),
  ~500-1400 640×400 (boot menu/text), ~1500+ 320×240 (game). Video ≈58.72 Hz.
- Audio emits a **variable** sample count per frame (~751 avg), hence the
  `--dump-audio-frames` sidecar. After Burner boot/title is **authentically
  silent** (FM/CDDA never triggered until attract mode) — not a mixer bug.

## Interactive SDL runner (`asuka_play`) — Task 6
- SDL2 via `find_package(SDL2 QUIET)`; guarded by `option(ASUKA_BUILD_SDL ON)`,
  so headless builds still produce `asuka_runner` (verified: `-DASUKA_BUILD_SDL=OFF`
  builds only `asuka_runner`). Debian pkg: `libsdl2-dev` (not preinstalled;
  `sudo apt-get install` works — resets with the container).
- Loop: poll events → fold analog-stick/d-pad into edge transitions → `step_frame`
  → upload framebuffer to a streaming `SDL_PIXELFORMAT_XRGB8888` texture
  (recreated on mode change) → `SDL_RenderSetLogicalSize` + integer scale +
  nearest → present. **Pace to the audio clock**: `SDL_QueueAudio` each frame,
  sleep only while queued > ~80 ms target (single `SDL_Delay`, no busy loop).
  Fallback 60 Hz when `--no-audio`. Flags: `--headless`, `--no-audio`,
  `--frames N`, `--width/--height`, `--vsync`, `--no-integer-scale`.
  Keys: arrows=d-pad, Enter=Start, Space/Z/X/C=fire, R=reset, F11=fullscreen,
  Esc=quit. Gamepad auto-detected (d-pad + left stick + face/shoulder).
- `--press <frame>:<key>` now takes **`asuka::Key` names** (up/down/left/right/
  start/select/a/b/x/y/l/r), not raw GT_Keys ints.
- Headless testing needs `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`; ctest
  `asuka_play_headless` runs the dummy engine 120 frames this way. Container has
  no display/audio device, so always set these for `asuka_play`.

## Verified / known-good
- `ctest` **4/4**: `asuka_smoke`, `asuka_audio_capture`, `asuka_fmtowns_loads`
  (WILL_FAIL, no-BIOS error path), `asuka_play_headless`.
- Clean rebuild 0 warnings (strict `-Wall -Wextra -Wpedantic -Wconversion …`);
  ASan+UBSan+LSan clean on dummy and 1300-frame real boot through `asuka_play`.
- Test assets are copyrighted → git-ignored `test_assets/`; fetch with
  `scripts/fetch_test_assets.sh` (BIOS `Abdess/retrobios`
  `bios/Fujitsu/FM Towns/`; media archive.org `fmtownscdcollection`). Verify
  boot with `scripts/verify_fmtowns_boot.sh`.
- **Vendor must stay pristine**: any debug instrumentation in
  `plugins/fmtowns/vendor/` must be reverted (`git status` clean; `grep -rc DBG`).
