# Project memory (Asuka)

Repo: `M5Devs/Asuka` — "Retro Vintage PC Runner & Modular Environment".
C++17 + CMake ≥ 3.16, GPLv3. Build: `cmake -B build && cmake --build build`.
Per-task detail lives in the daily logs (`YYYY-MM-DD.md`); this index is the
durable, cross-task subset.

## Architecture (verified)
- Libretro-inspired but object-oriented: the **host owns the loop** and *pulls*
  (`get_framebuffer`, `get_audio_samples`) after `step_frame()`; cores never
  drive it. Hosts: headless `asuka_runner` (`src/main.cpp`), interactive
  `asuka_play` (`src/interactive/main_sdl.cpp`), web (`src/web/main_web.cpp`).
- `include/asuka/IEngineCore.hpp` — pure-virtual ABI. Framebuffer **XRGB8888**
  (`uint32_t`); audio interleaved stereo `int16_t`, `sample_count` is *per
  channel*. Factories resolved by name: `create_engine`/`destroy_engine`.
- `EngineInstance` is RAII: `engine.reset()` runs `shutdown()` then
  `destroy_engine()`, and must happen before `loader.unload()`.
- `include/asuka/Input.hpp` — host `asuka::Key` enum (order: up=1,down=2,
  left=3,right=4,start=5,select=6,a=7,b=8,x=9,y=10,l=11,r=12). Hosts translate
  physical input to `asuka::Key`; each core maps those to its own codes.

## Git / push policy
- **Push straight to `origin/main`, no PR** (user's standing instruction). The
  credential in the `origin` URL is stale — push with
  `git push "https://${GITHUB_TOKEN}@github.com/M5Devs/Asuka.git" main`.
- Deploy repo `m5dev-tests/asukatest` likewise pushes to `main`.

## Environment / build gotchas
- Container reset wipes `/tmp` (incl. `/tmp/emsdk`) and uninstalls `cmake`;
  reinstall with `sudo apt-get install -y cmake` + emsdk `install/activate latest`.
- **emsdk build**: must `export EMSDK_PYTHON=/usr/bin/python3` (twice: before
  and after `emsdk_env.sh`) — the default `python3` shim segfaults on ctypes.
- Web build: `scripts/build_web.sh` → `web/dist/`. `ASUKA_BUILD_WEB` auto-ON
  under `EMSCRIPTEN`; native runners/plugins skipped. **No dlopen on WASM**:
  `plugins/fmtowns` splits into `asuka_fmtowns_core` (STATIC) +
  `asuka_fmtowns` (SHARED native) + `asuka_fmtowns_static` (STATIC web).
- `-sUSE_SDL=2` must be a **COMPILE** option too (else `fakesdl/SDL.h` `#error`).
  `-lidbfs.js` is required or `Module.IDBFS` is undefined;
  `-sEXPORTED_RUNTIME_METHODS` must include `FS,IDBFS,callMain,ccall,cwrap,
  UTF8ToString,HEAPU8`. Threads need COOP/COEP (`scripts/serve_web.py`,
  `web/_headers`; GitHub Pages uses `coi-serviceworker.min.js` instead).
- **Build is deterministic** — a clean rebuild reproduces byte-identical
  `asuka_web.wasm`/`.js`, so a rebuild is a valid deployed-binary check.
- Native tests: `ctest` **5/5** (`asuka_smoke`, `asuka_audio_capture`,
  `asuka_fmtowns_loads` WILL_FAIL, `asuka_snapshot`, `asuka_pc98`). Clean
  rebuild 0 warnings (`-Wall -Wextra -Wpedantic -Wconversion …`). Test assets
  are copyrighted → git-ignored `test_assets/` (`scripts/fetch_test_assets.sh`).
- **Vendor must stay pristine** (`plugins/*/vendor/`): revert any debug
  instrumentation (`git status` clean; `grep -rc DBG`).

## Engines
### FM Towns (Geartowns, `plugins/fmtowns`)
- Core + data deps only; desktop SDL3/OpenGL/imgui shell excluded (no GUI deps).
  `GT_DISABLE_DYNAREC` (interpreter). FB alloc 1024×768.
- Audio: Geartowns copies out the whole `m_buffer_index` capped at
  `GT_AUDIO_BUFFER_SIZE` (8192) — size the destination to that, not a
  per-frame estimate (was a heap-corruption bug). Emits a *variable* count
  per frame (~751 avg); After Burner boot/title is authentically silent.
- **Input: Game Port 1 (Towns Pad) is the real path for arcade titles.**
  `Input::SetGamePadState(port, state)` is a **level** (latch a button mask;
  clear on reset). Port 1 = I/O `0x04D0`. `asuka::Key`→pad: dirs→dirs,
  Start→RUN, Select→SELECT, A/B→A/B, X/Y→X/Y, L/R→Z/C.
- Boot: ~0-500 640×480 POST, ~500-1400 640×400 menu, ~1500+ 320×240 game;
  ≈58.72 Hz.

### PC-98 (NP2kai, `plugins/pc98`)
- Wraps NP2kai (submodule `uraraworks/NP2kai-wasm`, branch `wasm`) via its
  **Libretro** core (`-D__LIBRETRO__`) — the only headless pull-model entry.
  `retro_run()` == one frame.
- **Boots with NO external ROM** (built-in BIOS I/O emulation): 640×400 video,
  782 samples/ch audio. So `tests/pc98_test.sh` needs no copyrighted assets.
- **Scan codes are PC-98 hardware codes** (`keystat.h` NKEY_*): HELP=0x3f,
  STOP=0x60, ROLLUP=0x36, ROLLDOWN=0x37, RETURN=0x1c, F1=0x62..F5=0x66,
  A=0x1d, Z=0x29, SPACE=0x34, ESC=0x00. Inject via
  `keystat_senddata(code | (pressed ? 0 : 0x80))`.
- `retro_serialize` ORs informational bits (`STATFLAG_WARNING`/`VERCHG`) into
  its return — only `STATFLAG_FAILURE` (`-1`) is failure. Snapshot ≈21 MB +
  768,015-byte BMP. Save/load need one `retro_run()` first (prime lazily).
- **`IEngineCore::send_scancode(int,bool)`** (default no-op) carries keys the
  `asuka::Key` set can't express.

## Web shell (`web/index.html`) — current state
- PPSSPP-style shell: **games library** launcher, **pause drawer**, **settings
  modal**, floating **📁 ROM** picker + universal drag & drop, gamepad ⇄ PC-98
  virtual-keyboard toggle, 3-slot snapshot carousel.
- **Cores power on once per process** (NP2kai's `retro_run` gates a one-shot
  bootstrap on `static firstcall`; a 2nd in-process init runs a torn-down
  machine → WASM traps). **Never swap engines in place.** Any second boot (new
  game or machine switch) parks the image in IndexedDB (`asuka-game`/kv) and
  `location.reload()`s; boot reads it via `takePendingGame()`. Gate on
  `state.engineSpent`, not `state.running`.
- Library records keep the **original filename** (`file`) as well as the display
  `name` — `machineForName()` keys off the extension and the pretty title has
  none. IDB db `asuka-game` v2 adds the `games` store (`kv` = parked record).
- "Exit to library" reloads with `sessionStorage['asuka.exitToLibrary']="1"`;
  boot reads it to open the grid. With the library open on a fresh runtime,
  `loadRomFile` fetches the bundled BIOS itself (`loadBundledBios()`).
- C API (`src/web/main_web.cpp`): `asuka_web_load_rom(machine, media)`,
  `asuka_web_key`, `asuka_web_scancode`, `asuka_web_is_pc98`,
  `asuka_web_input_events` (counts input calls — the headless input proof),
  `asuka_web_set_paused`/`is_paused`, `asuka_web_set_volume`/`get_volume` (gain
  applied when queueing PCM — `SDL_QueueAudio` has no device volume),
  `asuka_web_swap_media` (re-calls `engine->load_media()`), snapshot APIs
  (`save/load_snapshot`, `has_snapshot`, `snapshot_thumbnail[_size]`).
- `asuka_web_init(bios_dir, media_path, machine)` (3rd arg default `"fmtowns"`).
  Machine from `?machine=` → `manifest.machine` → `"fmtowns"`.
- **Snapshot thumbnail = sidecar `.bmp`** (bottom-up 24-bit) written *after*
  `SaveState`; never embed it (Geartowns validates `header.size` == file
  length). Don't hand a `_malloc` buffer to JS — use a host scratch
  `std::vector` + a size getter. `FS.filesystems.IDBFS` is undefined; use
  `Module.IDBFS`, best-effort mount. Helpers in `extern "C"` must not return
  `std::string` (`-Wreturn-type-c-linkage`).
- **Verification gotchas:** SDL renders via WebGL without
  `preserveDrawingBuffer`, so `drawImage`/`getImageData` read blank — use a
  compositor screenshot. A hidden drawer uses `translateX`, so a "visible"
  check must reject off-screen rects, not just `display:none`. The deploy
  copy's coi-serviceworker reloads once (wait for `crossOriginIsolated` before
  uploading files).

## Deploy repo (`m5dev-tests/asukatest`)
- Static site = `web/dist` **plus** universal-host extras. Copy
  `asuka_web.{js,wasm}` + `index.html`, then **inject**
  `<script src="coi-serviceworker.min.js"></script>` right after `<head>`
  (GitHub Pages can't send COOP/COEP) and **keep the repo's own `_headers`**
  (adds `/assets/*` immutable caching + manifest revalidation).
- Turnkey auto-boot: bundled BIOS + media in `assets/`, described by
  `assets/manifest.json`; the shell streams them into `/bios` + `/media` then
  boots. `FMT_FNT.ROM` is required (ship all 7 firmware ROMs). Media must be
  split (GitHub 100 MB limit): per-track BINs + a multi-`FILE` CUE preserving
  PREGAP so the TOC matches. `_headers` caches `/assets/*` 1 yr.
- Caveat: the repo is **public** and hosts copyrighted BIOS + After Burner.
