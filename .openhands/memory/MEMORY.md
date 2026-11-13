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
  plugin)→`63df591`(audio fix)→`c1acb78`(recording)→`4ce8847`(boot audio)→
  `6ed3b4e`(asuka_play)→`f03749f`(WASM web shell).

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

## Web / WebAssembly (`asuka_web`) — Task 5
- Emscripten SDK **not preinstalled**; install to `/tmp/emsdk` (resets with the
  container) and `source /tmp/emsdk/emsdk_env.sh`. `em++`/`emcmake` required —
  plain `emcc` fails to link C++ (`undefined symbol: std::__2::thread…`).
- Build: `scripts/build_web.sh` (emcmake configure → build → stage `web/dist/`).
  `ASUKA_BUILD_WEB` auto-ON under `EMSCRIPTEN`; native runners/plugins skipped.
- **No dlopen on WASM**: `plugins/fmtowns` now splits into `asuka_fmtowns_core`
  (STATIC) + `asuka_fmtowns` (SHARED, native) + `asuka_fmtowns_static`
  (STATIC, web). The web target calls `create_engine`/`destroy_engine` directly
  via `plugins/fmtowns/fmtowns_plugin.hpp`.
- **`-sUSE_SDL=2` must be a COMPILE option too**, not just a link option, or
  `#include <SDL.h>` hits Emscripten's `fakesdl/SDL.h` stub (`#error "To use the
  emscripten port of SDL…"`). Set `target_compile_options(... -sUSE_SDL=2)`.
- Link flags that matter: `-sMODULARIZE=1 -sEXPORT_NAME=createAsukaModule`
  (factory the hand-written shell instantiates; no generated HTML shell),
  `-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728`, `-sPTHREAD_POOL_SIZE=8`
  (the vendored CUE/BIN reader uses `std::thread` read-ahead), `-lidbfs.js`
  (**required** or `Module.IDBFS` is undefined), `-sEXPORTED_RUNTIME_METHODS=`
  must include `FS,IDBFS,callMain,ccall,cwrap,UTF8ToString`.
- **Threads need cross-origin isolation** (COOP `same-origin` + COEP
  `require-corp`) for `SharedArrayBuffer`. `scripts/serve_web.py` sends them
  locally; `web/_headers` does it on Cloudflare Pages.
- Shell `web/index.html`: drag&drop + pickers (`.rom`→BIOS, `.bin/.cue/.chd/…`
  →media), disc selector, IndexedDB persistence (`FS.mount(Module.IDBFS)` over
  `/bios` and `/media`, then `FS.syncfs(true)`), touch pad, fullscreen. Exposes
  `window.asuka` for debugging.
- **Verification gotcha**: SDL renders via WebGL *without*
  `preserveDrawingBuffer`, so `ctx.drawImage(canvas)`/`getImageData` reads back
  **blank** even though video is fine. Use a compositor screenshot
  (puppeteer `elementHandle.screenshot()`) to check real output; a blank frame
  is a tiny PNG, a real FM Towns screen is much larger.
- Verified: WASM builds clean; headless Chromium (system `/usr/bin/chromium`,
  `--use-gl=swiftshader`) loads real BIOS + After Burner, boots, renders at
  **~57 fps** (717 frames/12 s, cross-origin isolated, 88 palette colours).

## Standalone web deploy (`m5dev-tests/asukatest`) — 2026-11-13
- The static site is deployed to a **separate repo**, `m5dev-tests/asukatest`
  (was LICENSE+README only; push straight to `main`). The current Asuka source
  lives in *another* workspace; the deploy target is a near-empty repo cloned
  for this purpose.
- Bundle = `web/dist/*` → repo root: `index.html`, `asuka_web.js`,
  `asuka_web.wasm`, `_headers`, plus universal-host extras `coi-serviceworker.min.js`,
  `.nojekyll`, `README.md`.
- **GitHub Pages has no custom headers**, so `coi-serviceworker.min.js`
  (gzuidhof, v0.1.7) is the isolation source there; its `<script>` must be the
  **first thing in `<head>`**. It registers a SW and reloads the page once.
  Cloudflare Pages still uses `_headers`.
- **IDBFS stall on the SW-isolated path (real bug, fixed)**: under
  coi-serviceworker, Emscripten's `FS.syncfs` never calls back — a plain
  `indexedDB.open('/media'|'/bios')` **times out**, while a random DB name
  succeeds and the *header* path is fine. `mountStorage()` awaited `syncfs`, so
  boot hung forever (canvas showed POST, frames stuck at 0). Fix in
  `web/index.html` `syncfs()`: 3 s timeout → set `idbfsSlow`, resolve and carry
  on (files stay in memory); later calls fire-and-forget so persistence can
  recover. Boot then works on both hosts (verified PASS: header-less **and**
  header path, real BIOS + After Burner).
- Verify a header-less deploy by serving with plain `python3 -m http.server`
  (no COOP/COEP) and booting via `/tmp/webtest/verify_coi.js` (waits for
  `crossOriginIsolated` before uploading — the SW reload wipes file inputs).
- Build: `source /tmp/emsdk/emsdk_env.sh && bash scripts/build_web.sh` (emcc
  7.0.0 in this container).

### Turnkey auto-boot deploy (commit `5716d3b`, 2026-11-13)
- Site now boots with **zero uploads**: bundled BIOS + After Burner live in
  `assets/`, described by `assets/manifest.json`; the shell streams them into
  `/bios` + `/media` then `asuka_web_init`+`start`. No manifest → falls back to
  the manual drop-zone. `mountFiles()` only clears a dir when the user supplied
  files for it, so manual Boot can't wipe auto-loaded assets.
- **`FMT_FNT.ROM` is REQUIRED** (`firmware.cpp IsRequired` excludes only
  FONT20/SERIAL/EXTENSION) — ship all 7: SYS, DOS, FNT, DIC, F20, MYTOWNS,
  MAR_EX0-3. `Abdess/retrobios` (`main`, `bios/Fujitsu/FM Towns/`) matches the
  known-good set byte-for-byte.
- **Media must be split**: GitHub's 100 MB hard limit rejects the 230 MB BIN.
  Split into 8 per-track BINs (2352 B sectors; range = [index01_lba*2352,
  next_index01_lba*2352)) + a **multi-`FILE` CUE** keeping each `PREGAP`; the
  reader's `total_pregap_length` keeps Track 2 at LBA 3180, i.e. the TOC is
  identical to the original. Non-URI paths use `DefaultLoadOptions`
  (`track_files_start_at_index1=false`).
- `_headers` caches `/assets/*` immutable (1 yr) and revalidates the manifest.
- Verified: fresh clone of the pushed repo served header-less auto-boots
  (7 firmware components, 8-track CUE, ~181 frames, touchpad auto-on).
- **Build is deterministic**: a clean `build_web.sh` reproduced byte-identical
  `asuka_web.wasm` (`d4569a57…`) / `.js` (`44d2afe9…`), so a rebuild is a valid
  check that deployed binaries match source.
- Caveat: the deploy repo is **public** and now hosts copyrighted BIOS +
  After Burner; the README says so and how to remove them.

### Minimalist shell + input fix (deploy `846dfe7`, 2026-11-13)
- **The web C entry point is `asuka_web_key(key, pressed)`** — there is no
  `asuka_web_send_input`. `key` is the `asuka::Key` value (Input.hpp order:
  up=1,down=2,left=3,right=4,start=5,select=6,a=7,b=8,x=9,y=10,l=11,r=12).
  FMTowns maps A→SPACE, B→Z, Start→RETURN, Select→SPACE.
- **Controls-silently-dead bug**: the old shell's `KEY` map used capitalised
  names (`Up`,`A`) while the touchpad `data-key` and `SCAN_TO_KEY` used
  lowercase (`up`,`a`), so `KEY[name]` was `undefined` and `sendKey` returned
  before calling the engine — no input ever registered. Keep the names lowercase
  everywhere and normalise in one send funnel.
- Keyboard sink is **idempotent** (`Keyboard::KeyPressed` early-returns on a
  repeat), so redundant JS + SDL2 paths are safe. Emscripten SDL2 listens on
  **`window`**, so keep window-level key listeners; still focus the canvas.
- **Game Port 1 (Towns Pad) is the real input path for arcade titles** (Task 6):
  `FMTownsEngine::send_input` used to feed only the FM Towns *keyboard*, so After
  Burner's title screen ignored every control. Geartowns: Game Port 1 = I/O
  `0x04D0` → `Input` index 0 (`GT_CONTROLLER_1`); `0x04D2` → index 1; the core
  only ever polls port 1. `Input::SetGamePadState(port, GT_GamePad_State{buttons})`
  is a **level** (keep a latched `gamepad_buttons_` mask; clear on reset/shutdown).
  `Input::ReadGamePad` maps `GT_GAMEPAD_*` to the 6 status lines; `0x3F` = idle.
  Port 1 defaults to `GT_CONTROLLER_ORIGINAL_GAMEPAD` (all buttons work).
  Key→pad: dirs→dirs, Start→RUN, Select→SELECT, A/B→A/B, X/Y→X/Y, L/R→Z/C.
  `GeartownsCore::GetInput()` returns the `Input*`.
- `asuka_web_input_events()` in `src/web/main_web.cpp` counts every
  `asuka_web_key` call (reset on init) — the headless test's proof that input
  reaches the engine.
- Minimal shell: pure-black body, stage `min(100vw,133.3333vh)` ×
  `min(75vw,100vh)` = exact 4:3 letterbox, `image-rendering:pixelated`. Pad =
  d-pad bottom-left, A/B bottom-right, START top-centre, shown only on touch
  devices. Loader fades out; failure = error + Retry (no upload UI at all).
- Container reset wipes `/tmp` (incl. `/tmp/emsdk`) and uninstalls `cmake`;
  reinstall with `sudo apt-get install -y cmake` + emsdk `install/activate latest`.

## Snapshots / save states — Task 7 (commit `58d23f6`)

- `IEngineCore::save_snapshot(path)` / `load_snapshot(path)` added; implemented
  in `FMTownsEngine` (Geartowns `SaveState`/`LoadState`, index -1 = verbatim
  path) and `DummyEngine` (frame counter only, exercises the ABI).
- **Thumbnail = sidecar `.bmp`** next to the state (`ThumbnailPath()`): a
  bottom-up 24-bit BMP written from the current framebuffer. NOT embedded in
  the state — Geartowns' desktop loader validates `header.size` against the
  exact file length, so stripping an embedded image would break the state.
  Write the BMP *after* `SaveState` succeeds; a missing thumbnail is non-fatal.
- Front-ends: `asuka_runner` `--save-snapshot`/`--load-snapshot`; `asuka_play`
  F5/F8 single slot (`--snapshot`, default `<media>.state`); web SNAP/LOAD +
  preview, persisted via IDBFS (`/bios/snapshots/slotN.{state,bmp}`).
- **Web gotchas (cost time):**
  - Emscripten does not export `Module.HEAPU8` unless listed in
    `-sEXPORTED_RUNTIME_METHODS=…,HEAPU8,…`. Without it, reading the thumbnail
    pointer threw and the JS silently stopped mid-handler (no log line).
    Guard the JS on `state.Module.HEAPU8` too.
  - Don't hand a `_malloc`-based buffer to JS (its pointer isn't in the
    exported runtime); instead keep a host-side scratch `std::vector<uint8_t>`
    and publish size via a getter (`asuka_web_snapshot_thumbnail_size()`).
  - `FS.filesystems.IDBFS` is undefined; use `Module.IDBFS` and make the mount
    best-effort (non-fatal).
  - Helpers inside the `extern "C"` block must not return `std::string`
    (`-Wreturn-type-c-linkage`); keep path helpers in the anonymous namespace.
- Test `tests/snapshot_test.sh` (ctest `asuka_snapshot`): asserts the BMP
  header/size and that 121 frames straight == state@120 + 1 frame (byte-exact).
  Skips when BIOS/media absent. **`ctest` now 5/5.**

## Verified / known-good
- `ctest` **5/5**: `asuka_smoke`, `asuka_audio_capture`, `asuka_fmtowns_loads`
  (WILL_FAIL, no-BIOS error path), `asuka_snapshot`, `asuka_play_headless`.
- Clean rebuild 0 warnings (strict `-Wall -Wextra -Wpedantic -Wconversion …`);
  ASan+UBSan+LSan clean on dummy and 1300-frame real boot through `asuka_play`.
- Test assets are copyrighted → git-ignored `test_assets/`; fetch with
  `scripts/fetch_test_assets.sh` (BIOS `Abdess/retrobios`
  `bios/Fujitsu/FM Towns/`; media archive.org `fmtownscdcollection`). Verify
  boot with `scripts/verify_fmtowns_boot.sh`.
- **Vendor must stay pristine**: any debug instrumentation in
  `plugins/fmtowns/vendor/` must be reverted (`git status` clean; `grep -rc DBG`).
