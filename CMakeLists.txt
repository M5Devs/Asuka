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

# Exercises the FM Towns plugin's load -> init error path (no firmware is
# available in CI, so init() is expected to fail gracefully).
if(TARGET asuka_fmtowns)
    add_test(NAME asuka_fmtowns_loads
        COMMAND asuka_runner --plugin "$<TARGET_FILE:asuka_fmtowns>")
    set_tests_properties(asuka_fmtowns_loads PROPERTIES WILL_FAIL TRUE)
endif()
