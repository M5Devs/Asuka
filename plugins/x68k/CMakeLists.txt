# Sharp X68000 engine, wrapping the PX68k core.
#
# PX68k's Libretro front-end (libretro.c, built with -D__LIBRETRO__) is the
# only headless, pull-model entry point, so that is what the Asuka glue drives,
# exactly as the PC-98 plugin drives NP2kai's. The desktop GUI stacks are not
# needed and are not built.
#
# The engine is split the same way as the other two plugins:
#   * asuka_x68k_core   - STATIC: the engine glue + the whole PX68k core.
#   * asuka_x68k        - SHARED: the dynamic plugin for the desktop runners.
#   * asuka_x68k_static - STATIC: the plugin entry points for the web build,
#                                  which links the engine in directly because
#                                  dlopen across WASM modules is brittle.

set(PX68K_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/PX68k")
set(PX68K_LC "${PX68K_ROOT}/libretro-common")

if(NOT EXISTS "${PX68K_ROOT}/libretro.c")
    message(FATAL_ERROR
        "PX68k sources are missing. Fetch the submodule with:\n"
        "  git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# PX68k core sources.
#
# The directory layout mirrors PX68k's own Makefile.common, so the set stays in
# step with the vendored tree (which is pinned, making the globs deterministic).
# Two deliberate departures from upstream:
#   * the internal MIDI synthesizers (SC-55 and the bundled Munt MT-32) are
#     replaced by x68k_midi_stub.c. They are large and the Munt half spawns a
#     worker thread; the X68000's own YM2151/ADPCM audio is unaffected.
# The CPU build matches upstream's CPU_RUNTIME=1 default: both the C68K and
# Musashi 68000 cores are compiled and selected at runtime (the core's own
# save-state code and option handling assume that pairing).
# ---------------------------------------------------------------------------
set(PX68K_SOURCES
    "${PX68K_ROOT}/libretro.c"
    # --- M68000 CPU (C68K + Musashi, runtime-selected) ---
    "${PX68K_ROOT}/m68000/m68000.c"
    "${PX68K_ROOT}/m68000/m68k/c68k.c"
    "${PX68K_ROOT}/m68000/m68k/c68kexec.c"
    "${PX68K_ROOT}/m68000/musashi/m68kcpu.c"
    "${PX68K_ROOT}/m68000/musashi/m68kops.c"
    "${PX68K_ROOT}/m68000/musashi/softfloat/softfloat.c"
    # --- X68000 subsystems ---
    "${PX68K_ROOT}/x68k/adpcm.c"
    "${PX68K_ROOT}/x68k/bg.c"
    "${PX68K_ROOT}/x68k/crtc.c"
    "${PX68K_ROOT}/x68k/dmac.c"
    "${PX68K_ROOT}/x68k/fdc.c"
    "${PX68K_ROOT}/x68k/fdd.c"
    "${PX68K_ROOT}/x68k/disk_d88.c"
    "${PX68K_ROOT}/x68k/disk_dim.c"
    "${PX68K_ROOT}/x68k/disk_xdf.c"
    "${PX68K_ROOT}/x68k/gvram.c"
    "${PX68K_ROOT}/x68k/ioc.c"
    "${PX68K_ROOT}/x68k/irqh.c"
    "${PX68K_ROOT}/x68k/mem_wrap.c"
    "${PX68K_ROOT}/x68k/mercury.c"
    "${PX68K_ROOT}/x68k/mfp.c"
    "${PX68K_ROOT}/x68k/palette.c"
    "${PX68K_ROOT}/x68k/midi.c"
    "${PX68K_ROOT}/x68k/pia.c"
    "${PX68K_ROOT}/x68k/rtc.c"
    "${PX68K_ROOT}/x68k/sasi.c"
    "${PX68K_ROOT}/x68k/scc.c"
    "${PX68K_ROOT}/x68k/scsi.c"
    "${PX68K_ROOT}/x68k/sram.c"
    "${PX68K_ROOT}/x68k/sysport.c"
    "${PX68K_ROOT}/x68k/tvram.c"
    # --- Libretro host layer ---
    "${PX68K_ROOT}/libretro/joystick.c"
    "${PX68K_ROOT}/libretro/keyboard.c"
    "${PX68K_ROOT}/libretro/mouse.c"
    "${PX68K_ROOT}/libretro/prop.c"
    "${PX68K_ROOT}/libretro/timer.c"
    "${PX68K_ROOT}/libretro/dswin.c"
    "${PX68K_ROOT}/libretro/windraw.c"
    "${PX68K_ROOT}/libretro/winui.c"
    "${PX68K_ROOT}/libretro/dosio.c"
    "${PX68K_ROOT}/libretro/fake.c"
    "${PX68K_ROOT}/libretro/peace.c"
    "${PX68K_ROOT}/libretro/zipdisk.c"
    "${PX68K_ROOT}/libretro/state.c"
    # --- MIDI state tracking (the synth backend is stubbed out) ---
    "${PX68K_ROOT}/midi/midi_shadow.c"
    # --- fmgen (YM2151 OPM + YM2608 OPNA) ---
    "${PX68K_ROOT}/fmgen/fmg_wrap.cpp"
    "${PX68K_ROOT}/fmgen/fmgen.cpp"
    "${PX68K_ROOT}/fmgen/fmtimer.cpp"
    "${PX68K_ROOT}/fmgen/opm.cpp"
    "${PX68K_ROOT}/fmgen/opna.cpp"
    "${PX68K_ROOT}/fmgen/psg.cpp")

# libretro-common: the support library the Libretro front-end relies on
# (filestream/VFS, dirent, string helpers, UTF-8, CRC/deflate for the ZIP
# reader, CPU feature detection). This is the exact non-static set PX68k's
# Makefile.libretro builds for the USE_LIBRETRO_VFS path.
set(PX68K_LC_SOURCES
    "${PX68K_LC}/compat/compat_posix_string.c"
    "${PX68K_LC}/compat/compat_strcasestr.c"
    "${PX68K_LC}/compat/compat_snprintf.c"
    "${PX68K_LC}/compat/compat_strl.c"
    "${PX68K_LC}/compat/fopen_utf8.c"
    "${PX68K_LC}/encodings/encoding_crc32.c"
    "${PX68K_LC}/encodings/encoding_deflate.c"
    "${PX68K_LC}/encodings/encoding_utf.c"
    "${PX68K_LC}/features/features_cpu.c"
    "${PX68K_LC}/file/file_path.c"
    "${PX68K_LC}/file/file_path_io.c"
    "${PX68K_LC}/file/retro_dirent.c"
    "${PX68K_LC}/formats/zip/rzip_archive.c"
    "${PX68K_LC}/rthreads/rthreads.c"
    "${PX68K_LC}/streams/file_stream.c"
    "${PX68K_LC}/streams/file_stream_transforms.c"
    "${PX68K_LC}/string/stdstring.c"
    "${PX68K_LC}/time/rtime.c"
    "${PX68K_LC}/vfs/vfs_implementation.c")

# ---------------------------------------------------------------------------
# Engine core (static)
# ---------------------------------------------------------------------------
add_library(asuka_x68k_core STATIC
    X68kEngine.cpp
    x68k_midi_stub.c
    ${PX68K_SOURCES}
    ${PX68K_LC_SOURCES})

target_include_directories(asuka_x68k_core PUBLIC
    "${CMAKE_SOURCE_DIR}/include")
target_include_directories(asuka_x68k_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")

# Vendor headers are treated as system headers so their diagnostics stay out
# of our glue compilation.
target_include_directories(asuka_x68k_core SYSTEM PRIVATE
    "${PX68K_ROOT}"
    "${PX68K_ROOT}/libretro"
    "${PX68K_ROOT}/x68k"
    "${PX68K_ROOT}/fmgen"
    "${PX68K_ROOT}/m68000"
    "${PX68K_ROOT}/m68000/c68k"
    "${PX68K_ROOT}/m68000/m68k"
    "${PX68K_ROOT}/m68000/musashi"
    "${PX68K_LC}/include")

# The definition set matches PX68k's own Libretro build (Makefile.common +
# Makefile.libretro): the runtime-selectable 68000 core pair, the Libretro VFS,
# and NO_MERCURY (the optional Mercury sound board, off upstream too).
target_compile_definitions(asuka_x68k_core PRIVATE
    __LIBRETRO__
    USE_LIBRETRO_VFS
    NO_MERCURY
    HAVE_M68000
    HAVE_MUSASHI
    HAVE_CPU_RUNTIME
    PX68K_VERSION="0.15+"
    GIT_VERSION="asuka")

target_link_libraries(asuka_x68k_core PRIVATE asuka_warnings)

# Keep the engine internals out of the plugin's dynamic symbol table; only the
# ASUKA_EXPORT factory entry points stay visible.
set_target_properties(asuka_x68k_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Silence warnings for vendored code only (appended after target options).
set_source_files_properties(
    ${PX68K_SOURCES} ${PX68K_LC_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-w")

# PX68k's C sources rely on implicit declarations and incompatible function
# pointer casts, which newer GCC promotes to errors; relax them exactly as the
# upstream Makefile does. Its C++ sources need -fpermissive and the framerate
# math is float-heavy, so allow the implicit conversions fmgen assumes.
target_compile_options(asuka_x68k_core PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>
    $<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-function-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wno-int-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-narrowing>)

# PX68k's headers rely on compiler.h being seen first for their integer
# typedefs, and the symbol-isolation shim must precede every PX68k header too.
# Both are pulled in from one wrapper: CMake de-duplicates repeated -include
# flags, so they cannot be passed as two separate options.
target_compile_options(asuka_x68k_core PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/x68k_force_include.h")

# --- Desktop plugin (shared) -------------------------------------------------
# Native only: the web build links asuka_x68k_static instead.
if(NOT ASUKA_BUILD_WEB)
    add_library(asuka_x68k SHARED
        x68k_plugin.cpp)

    target_compile_definitions(asuka_x68k PRIVATE ASUKA_PLUGIN_SHARED)

    target_link_libraries(asuka_x68k PRIVATE asuka_x68k_core asuka_warnings)

    set_target_properties(asuka_x68k PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/x68k"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/x68k")
endif()

# --- Static plugin (web) -----------------------------------------------------
# The web build links the engine in directly instead of dlopen-ing a module.
# The generic create_engine/destroy_engine names are not exported here: the FM
# Towns engine is linked into the same module and owns those.
if(ASUKA_BUILD_WEB)
    add_library(asuka_x68k_static STATIC
        x68k_plugin.cpp)
    target_link_libraries(asuka_x68k_static PRIVATE
        asuka_x68k_core asuka_warnings)
endif()
