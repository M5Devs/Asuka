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
