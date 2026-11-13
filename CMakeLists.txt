cmake_minimum_required(VERSION 3.16)

project(Asuka
    VERSION 0.1.0
    DESCRIPTION "Modular vintage PC runner"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Default to a single, predictable output tree so the runner can locate the
# plugin without hand-tuned paths.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Make the build relocatable: encode the build/install tree origin so
# executables can find their sibling shared libraries without needing
# LD_LIBRARY_PATH (or PATH on Windows). Nothing currently links a shared
# Asuka library, but this keeps the layout correct as cores are added.
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
set(CMAKE_BUILD_RPATH "$ORIGIN")
set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# ---------------------------------------------------------------------------
# Web build option.
#
# When the Emscripten toolchain drives CMake (emcmake cmake ...), ASUKA_BUILD_WEB
# defaults on and the native runners/plugins are skipped in favour of the single
# asuka_web module. It can also be requested explicitly on a native configure to
# only build the shared pieces the web target needs.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set(_asuka_web_default ON)
else()
    set(_asuka_web_default OFF)
endif()
option(ASUKA_BUILD_WEB "Build the WebAssembly (Emscripten) target" ${_asuka_web_default})

# ---------------------------------------------------------------------------
# Warnings: strict for our own targets (not third-party ones).
# ---------------------------------------------------------------------------
add_library(asuka_warnings INTERFACE)

if(MSVC)
    target_compile_options(asuka_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->
        $<$<COMPILE_LANGUAGE:C>:/W4>)
else()
    # C++-specific diagnostics must not leak into C sources (vendored code).
    target_compile_options(asuka_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wunused
        $<$<COMPILE_LANGUAGE:CXX>:
            -Wnon-virtual-dtor -Woverloaded-virtual -Wconversion -Wsign-conversion>)
endif()

# ---------------------------------------------------------------------------
# Core library: the loader and (future) shared runtime.
#
# The loader is native-only: it wraps dlopen/LoadLibrary, which do not apply to
# a statically linked WASM module.
# ---------------------------------------------------------------------------
if(NOT ASUKA_BUILD_WEB)
    add_library(asuka_core STATIC
        src/core/PluginLoader.cpp)

    target_include_directories(asuka_core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

    target_link_libraries(asuka_core PRIVATE asuka_warnings)

    set_target_properties(asuka_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    if(NOT MSVC)
        # dlopen/dlsym/dlclose live in libdl on Linux and macOS.
        find_library(ASUKA_DL_LIB NAMES dl)
        if(ASUKA_DL_LIB)
            target_link_libraries(asuka_core PUBLIC ${ASUKA_DL_LIB})
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# CLI runner.
# ---------------------------------------------------------------------------
if(NOT ASUKA_BUILD_WEB)
    add_executable(asuka_runner src/main.cpp)

    target_link_libraries(asuka_runner PRIVATE asuka_core asuka_warnings)
endif()

# ---------------------------------------------------------------------------
# Interactive SDL runner (optional).
#
# Builds asuka_play, the real-time runner with a window, live audio and input.
# It is only built when SDL2 is found, so headless/server builds that lack SDL
# still produce asuka_runner. Point CMAKE_PREFIX_PATH at an SDL2 install or
# turn the option off to skip it explicitly.
# ---------------------------------------------------------------------------
option(ASUKA_BUILD_SDL "Build the interactive SDL runner (asuka_play)" ON)
if(ASUKA_BUILD_SDL AND NOT ASUKA_BUILD_WEB)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        add_executable(asuka_play src/interactive/main_sdl.cpp)
        target_link_libraries(asuka_play PRIVATE asuka_core asuka_warnings)
        if(TARGET SDL2::SDL2)
            target_link_libraries(asuka_play PRIVATE SDL2::SDL2)
        else()
            # Older SDL2 packages export SDL2_INCLUDE_DIRS / SDL2_LIBRARIES.
            target_include_directories(asuka_play PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(asuka_play PRIVATE ${SDL2_LIBRARIES})
        endif()
        message(STATUS "Interactive runner enabled: asuka_play (SDL2)")
    else()
        message(STATUS
            "Interactive runner disabled: SDL2 not found "
            "(install libsdl2-dev or set -DASUKA_BUILD_SDL=OFF)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Plugins.
#
# The dummy plugin is a native shared library used by the desktop runners, so
# it is skipped for the web build.
# ---------------------------------------------------------------------------
if(NOT ASUKA_BUILD_WEB)
    add_subdirectory(plugins/dummy)

    # Place the dummy plugin where the runner looks by default.
    set_target_properties(asuka_dummy_engine PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/plugins/dummy"
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/plugins/dummy")
endif()

# FM Towns engine (Geartowns). Enabled only when the submodule is present so a
# plain checkout still configures; see plugins/fmtowns/CMakeLists. On the web
# build this contributes the static engine instead of the shared plugin.
option(ASUKA_BUILD_FMTOWNS "Build the FM Towns (Geartowns) engine" ON)
if(ASUKA_BUILD_FMTOWNS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/fmtowns/vendor/Geartowns/src/geartowns_core.cpp")
        add_subdirectory(plugins/fmtowns)
    else()
        message(WARNING
            "FM Towns engine skipped: Geartowns submodule not initialised. "
            "Run: git submodule update --init --recursive")
    endif()
endif()

# ---------------------------------------------------------------------------
# Web target (Emscripten + SDL2).
#
# One WASM module with the engine linked in directly. SDL2 provides the canvas
# and Web Audio backends; IDBFS persists mounted BIOS/media across reloads.
# ---------------------------------------------------------------------------
if(ASUKA_BUILD_WEB)
    if(NOT TARGET asuka_fmtowns_static)
        message(FATAL_ERROR
            "Web build needs the FM Towns engine. Initialise the submodule:\n"
            "  git submodule update --init --recursive")
    endif()

    add_executable(asuka_web src/web/main_web.cpp)

    target_link_libraries(asuka_web PRIVATE
        asuka_fmtowns_static asuka_warnings)

    target_include_directories(asuka_web PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/fmtowns")

    # -sUSE_SDL=2 must be set at compile time too, so <SDL.h> resolves to the
    # Emscripten SDL2 port header (otherwise it hits the fakesdl stub).
    target_compile_options(asuka_web PRIVATE -sUSE_SDL=2)

    # -O3: fast. ALLOW_MEMORY_GROWTH: the emulator's heap is large and grows.
    # PTHREADS: the vendored CD reader spawns a read-ahead thread. SDL2 supplies
    # the canvas + Web Audio. MODULARIZE turns the output into a factory the
    # hand-written web/index.html instantiates itself, so no generated shell is
    # needed. FORCE_FILESYSTEM + IDBFS back the mounted /bios and /media.
    target_link_options(asuka_web PRIVATE
        -O3
        -sALLOW_MEMORY_GROWTH=1
        -sUSE_SDL=2
        -sPTHREAD_POOL_SIZE=8
        -sINITIAL_MEMORY=134217728
        -sSTACK_SIZE=5242880
        -sEXIT_RUNTIME=0
        -sFORCE_FILESYSTEM=1
        -sMODULARIZE=1
        -sEXPORT_NAME=createAsukaModule
        # HEAPU8 lets the shell copy a snapshot thumbnail out of the WASM heap.
        -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPU8,callMain,ccall,cwrap,UTF8ToString
        -sENVIRONMENT=web,worker
        # IndexedDB-backed filesystem for the persistent /bios and /media.
        -lidbfs.js)

    # Name the artefacts so the shell can find them: asuka_web.js / .wasm.
    set_target_properties(asuka_web PROPERTIES
        OUTPUT_NAME "asuka_web"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web")

    message(STATUS "Web target enabled: asuka_web (Emscripten)")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NOT ASUKA_BUILD_WEB)
    enable_testing()

    add_test(NAME asuka_smoke
        COMMAND asuka_runner
                "$<TARGET_FILE:asuka_dummy_engine>")

    # Exercises the runner's audio capture against the dummy engine: a valid
    # 16-bit stereo WAV, a per-frame sample map that matches it, and non-silent
    # output. Needs python3 with numpy for the WAV checks, so it is only
    # registered when that is available.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy"
                        RESULT_VARIABLE _asuka_numpy_result
                        OUTPUT_QUIET ERROR_QUIET)
        if(_asuka_numpy_result EQUAL 0)
            add_test(NAME asuka_audio_capture
                COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_capture_test.sh"
                        "$<TARGET_FILE:asuka_runner>"
                        "$<TARGET_FILE:asuka_dummy_engine>")
        endif()
    endif()

    # Exercises the FM Towns plugin's load -> init error path (no firmware is
    # available in CI, so init() is expected to fail gracefully).
    if(TARGET asuka_fmtowns)
        add_test(NAME asuka_fmtowns_loads
            COMMAND asuka_runner --plugin "$<TARGET_FILE:asuka_fmtowns>")
        set_tests_properties(asuka_fmtowns_loads PROPERTIES WILL_FAIL TRUE)

        # Visual snapshots: save/load a state with a BMP thumbnail and check the
        # state round-trips deterministically. Skips when the FM Towns BIOS or
        # media image is not present.
        add_test(NAME asuka_snapshot
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/snapshot_test.sh"
                    "$<TARGET_FILE:asuka_runner>"
                    "$<TARGET_FILE:asuka_fmtowns>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test_assets/bios"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test_assets/media/Afterburner (1987)(Sega)(Jp-En).cue")
    endif()

    # Drives the dummy engine through the interactive SDL runner's whole loop
    # with SDL's dummy video/audio backends, so it needs no display or sound
    # device.
    if(TARGET asuka_play)
        add_test(NAME asuka_play_headless
            COMMAND asuka_play
                    --plugin "$<TARGET_FILE:asuka_dummy_engine>"
                    --frames 120 --headless)
        set_tests_properties(asuka_play_headless PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    endif()
endif()
