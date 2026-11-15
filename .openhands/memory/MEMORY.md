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
- **emsdk build**: `export EMSDK_PYTHON=/usr/bin/python3` (twice: before and
  after `emsdk_env.sh`); the default `python3` shim segfaults on ctypes.
- Web build: `scripts/build_web.sh` → `web/dist/`. `ASUKA_BUILD_WEB` auto-ON
  under `EMSCRIPTEN`; native runners/plugins skipped. **No dlopen on WASM**:
  `plugins/fmtowns` splits into `asuka_fmtowns_core` (STATIC) +
  `asuka_fmtowns` (SHARED native) + `asuka_fmtowns_static` (STATIC web).
- `-sUSE_SDL=2` must be a **COMPILE** option too (else `fakesdl/SDL.h` `#error`).
  `-lidbfs.js` is required or `Module.IDBFS` is undefined;
  `-sEXPORTED_RUNTIME_METHODS` needs `FS,IDBFS,callMain,ccall,cwrap,
  UTF8ToString,HEAPU8`. Threads need COOP/COEP (`scripts/serve_web.py`,
  `web/_headers`; GitHub Pages uses `coi-serviceworker.min.js`).
- **Build is deterministic** — a clean rebuild reproduces byte-identical
  `asuka_web.wasm`/`.js`, so a rebuild is a valid deployed-binary check.
- Native tests: `ctest` **6/6** (`asuka_smoke`, `asuka_audio_capture`,
  `asuka_fmtowns_loads` WILL_FAIL, `asuka_snapshot`, `asuka_pc98`,
  `asuka_x68k`). Clean rebuild 0 warnings (`-Wall -Wextra -Wpedantic
  -Wconversion …`). Test assets are copyrighted → git-ignored `test_assets/`
  (`scripts/fetch_test_assets.sh`).
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

### X68000 (PX68k, `plugins/x68k`) — third core
- Wraps **PX68k** (submodule `plugins/x68k/vendor/PX68k`,
  `libretro/px68k-libretro`) via Libretro, like the PC-98 adapter. 768×512
  RGB565→XRGB8888. Register vendor as a **submodule** (gitlink mode `160000`),
  matching PC-98/FM Towns — not a nested clone.
- **315 strong symbols collide** with NP2kai (both vendor Libretro +
  libretro-common + fmgen). wasm-ld can't scope per-symbol; llvm-objcopy can't
  rename wasm symbols. Fix = compile-time prefix shim
  `plugins/x68k/x68k_symbol_prefix.h` (renames 178 C + 137 C++ names to
  `px68k_*`), force-included via **one** wrapper `x68k_force_include.h`
  (two `-include` flags break CMake: "cannot specify '-o' with '-c'").
- **Keys are Libretro RetroKey codes** (RETURN=13, ESCAPE=27, arrows
  273/274/276/275, F1..F5 282..286, letters ASCII) — *not* PC-98 codes.
  PX68k polls `RETRO_DEVICE_KEYBOARD`; `send_scancode()` stores them in
  `g_held_keys_kbd[]`. Test skips without `test_assets/x68k_bios`.

## Web shell (`web/index.html`) — current state
- PPSSPP-style shell: **games library** launcher, **pause drawer**, **settings
  modal**, floating **📁 ROM** picker + universal drag & drop, gamepad ⇄ soft
  keyboard toggle (PC-98 + X68000), 3-slot snapshot carousel with
  DEL/EXP/IMP. Title "Asuka — FM Towns / PC-98 / X68000".
- **Cores power on once per process** (a 2nd in-process init runs a torn-down
  machine → WASM traps; probed: FM→FM ok, 2nd PC-98 fails, FM-after-PC98 traps
  `null function`). **Never swap engines in place.** Any second boot (new game
  or machine switch) parks the image in IndexedDB (`asuka-game`/kv) and
  `location.reload()`s; boot reads it via `takePendingGame()`. Gate on
  `state.engineSpent`, not `state.running`. "Exit to library" is *in-place*
  (`asuka_web_stop`+`shutdown`, `showLibrary()`), no reload.
- **Home = the library, not a boot.** `DOMContentLoaded` boots only when
  `?boot=` / `?machine=` is present **or** a game is parked (`hasPendingGame()`
  = non-destructive `idbGet("pending")` peek); else `loaderDone();
  showLibrary()`. `body.library-mode` hides `#stage/#pad/#kbd/#tools`;
  `loaderShow()` re-arms the loader for an in-place boot. Library records keep
  the **original filename** (`file`) — `machineForName()` keys off the
  extension. Bundled demo = one manifest-driven `featured` card.
- `machineForName` extensions: PC-98 (`.hdi/.d88/.fdi/.nhd/.thd`), X68000
  (`.dim/.xdf/.hds/.hdf/.m3u`), FM Towns (`.chd/.iso/.cue`) + `.zip`.
  `state.x68k` opens the keyboard surface. Snapshot EXP downloads the `.state`,
  IMP writes one back.
- C API (`src/web/main_web.cpp`): `asuka_web_load_rom(bios_dir, machine,
  media_path)` (3-arg; `bios_dir` sets `g_state.bios_dir` for the cold-start
  ROM-picker/drag-drop path — was a bug), `asuka_web_key`, `asuka_web_scancode`,
  `asuka_web_is_pc98`, `asuka_web_is_x68k`, `asuka_web_machine`,
  `asuka_web_input_events` (headless input proof), `set_paused`/`is_paused`,
  `set_volume`/`get_volume` (gain at PCM queue time — `SDL_QueueAudio` has no
  device volume), `swap_media`, snapshot APIs (`save/load/delete_snapshot`,
  `has_snapshot`, `snapshot_thumbnail[_size]`).
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
  copy's coi-serviceworker reloads once (wait for `crossOriginIsolated`).
- Headless test harness: puppeteer-core 23 at `/tmp/smoketest` +
  `/usr/bin/chromium` (Playwright's `/opt/google/chrome/chrome` is absent);
  serve via `scripts/serve_web.py <dir>` (COOP/COEP) on :8000.

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

## Multi-demo manifest + on-device devtools — Task 9 (Asuka `78da2fc`)
- `assets/manifest.json` may now list `"demos": [{id,title,machine,glyph,
  featured,bios[],media[],cue}]` instead of the single-title shape. The library
  shows one card per demo (both machines at once); the shell resolves the boot
  asset set per machine via `resolveDemo()` (falls back to the single-title
  shape, so old deployments keep working). `loadAssetEntries()`+`tagAssetDirs()`
  replaced `loadManifestAssets()`. **X68000 firmware = just `iplrom.dat` +
  `cgrom.dat`** (917 KB); PX68k opens them by exactly those names, so they must
  be mounted lowercase in the bios dir.
- Eruda devtools injected in `web/index.html` `<head>` from jsdelivr (guarded:
  `if (window.eruda)`). jsdelivr sends `cross-origin-resource-policy:
  cross-origin`, so it loads fine under COEP. Entry button is inside a **shadow
  root** — reach it via `#eruda.shadowRoot.querySelector('.eruda-entry-btn')`.
  Default position is bottom-right (collides with the gamepad), so we
  `eruda.position({x,y})` it up the right edge.
- Machine switch still needs a reload (vendored cores power on once); verified
  x68k→pc98 by dropping a `.d88` onto a running X68000 (parks + reloads).
- Test harness scripts in `/tmp/smoketest`: `library.mjs` (cards+eruda+x68k card
  boot), `dropdim.mjs`/`dropzip.mjs` (drag-drop real `.dim` and `.zip`),
  `fmtowns.mjs` (FM Towns regression), `switch.mjs`, `live.mjs` (live Pages).
