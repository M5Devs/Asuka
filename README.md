# Asuka

A cross-platform, modular **vintage PC runner**: a host application that loads
emulation cores as dynamically-linked plugins behind a single clean C++
interface.

Licensed under the [GNU General Public License v3.0](LICENSE).

## Status

Task 1 — foundation. The skeleton, the plugin ABI (`IEngineCore`), the
cross-platform dynamic loader, a reference *dummy* plugin and a CLI test runner
are implemented and verified.

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
CMakeLists.txt
```

## Build

Requires CMake ≥ 3.16 and a C++17 compiler.

```sh
cmake -B build
cmake --build build
```

## Run

```sh
./build/bin/asuka_runner
# or point it explicitly at a plugin:
./build/bin/asuka_runner build/plugins/dummy/libasuka_dummy_engine.so
# or via the environment:
ASUKA_PLUGIN_PATH=/path/to/plugin.so ./build/bin/asuka_runner
```

The runner loads the plugin, initialises it, runs 60 frames while reporting the
framebuffer size, the centre pixel and the audio sample count, then tears
everything down and unloads the library.

## Test

```sh
ctest --test-dir build --output-on-failure
```
