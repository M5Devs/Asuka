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
# ---------------------------------------------------------------------------
add_library(asuka_core STATIC
    src/core/PluginLoader.cpp)

target_include_directories(asuka_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(asuka_core PRIVATE asuka_warnings)

set_target_properties(asuka_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

if(NOT MSVC)
    # dlopen/dlsym/dlclose live in libdl on Linux and macOS.
    find_library(CMAKE_DL_LIBS NAMES dl)
    if(CMAKE_DL_LIBS)
        target_link_libraries(asuka_core PUBLIC ${CMAKE_DL_LIBS})
    endif()
endif()

# ---------------------------------------------------------------------------
# CLI runner.
# ---------------------------------------------------------------------------
add_executable(asuka_runner src/main.cpp)

target_link_libraries(asuka_runner PRIVATE asuka_core asuka_warnings)

# ---------------------------------------------------------------------------
# Interactive SDL runner (optional).
#
# Builds asuka_play, the real-time runner with a window, live audio and input.
# It is only built when SDL2 is found, so headless/server builds that lack SDL
# still produce asuka_runner. Point CMAKE_PREFIX_PATH at an SDL2 install or
# turn the option off to skip it explicitly.
# ---------------------------------------------------------------------------
option(ASUKA_BUILD_SDL "Build the interactive SDL runner (asuka_play)" ON)
if(ASUKA_BUILD_SDL)
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
# ---------------------------------------------------------------------------
add_subdirectory(plugins/dummy)

# Place the dummy plugin where the runner looks by default.
set_target_properties(asuka_dummy_engine PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/plugins/dummy"
    RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/plugins/dummy")

# FM Towns engine plugin (Geartowns). Enabled only when the submodule is
# present so a plain checkout still configures; see plugins/fmtowns/CMakeLists.
option(ASUKA_BUILD_FMTOWNS "Build the FM Towns (Geartowns) plugin" ON)
if(ASUKA_BUILD_FMTOWNS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/fmtowns/vendor/Geartowns/src/geartowns_core.cpp")
        add_subdirectory(plugins/fmtowns)
    else()
        message(WARNING
            "FM Towns plugin skipped: Geartowns submodule not initialised. "
            "Run: git submodule update --init --recursive")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_test(NAME asuka_smoke
    COMMAND asuka_runner
            "$<TARGET_FILE:asuka_dummy_engine>")

# Exercises the runner's audio capture against the dummy engine: a valid 16-bit
# stereo WAV, a per-frame sample map that matches it, and non-silent output.
# Needs python3 with numpy for the WAV checks, so it is only registered when
# that is available.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy"
                    RESULT_VARIABLE _asuka_numpy_result OUTPUT_QUIET ERROR_QUIET)
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
endif()

# Drives the dummy engine through the interactive SDL runner's whole loop with
# SDL's dummy video/audio backends, so it needs no display or sound device.
if(TARGET asuka_play)
    add_test(NAME asuka_play_headless
        COMMAND asuka_play
                --plugin "$<TARGET_FILE:asuka_dummy_engine>"
                --frames 120 --headless)
    set_tests_properties(asuka_play_headless PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
