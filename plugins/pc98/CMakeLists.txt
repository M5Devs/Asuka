# PC-98 / PC-9821 engine, wrapping the NP2kai (Neko Project II kai) core.
#
# NP2kai's Libretro front-end (sdl/libretro/libretro.c, built with
# -D__LIBRETRO__) is the only headless, pull-model entry point, so that is what
# the Asuka glue drives. The desktop GUI stacks (SDL, wxWidgets, X11) are not
# needed and are not built.
#
# The engine is split the same way as the FM Towns plugin:
#   * asuka_pc98_core   - STATIC: the engine glue + the whole NP2kai core.
#   * asuka_pc98        - SHARED: the dynamic plugin for the desktop runners.
#   * asuka_pc98_static - STATIC: the plugin entry points for the web build,
#                                  which links the engine in directly because
#                                  dlopen across WASM modules is brittle.

set(NP2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/NP2kai")
set(NP2_LC "${NP2_ROOT}/sdl/libretro/libretro-common")

if(NOT EXISTS "${NP2_ROOT}/sdl/libretro/libretro.c")
    message(FATAL_ERROR
        "NP2kai sources are missing. Fetch the submodule with:\n"
        "  git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# NP2kai core sources.
#
# The directory layout mirrors NP2kai's own Makefile.common / CMakeLists.txt,
# so the set stays in step with the vendored tree (which is pinned, making the
# globs deterministic).
# ---------------------------------------------------------------------------
file(GLOB NP2_COMMON_SOURCES
    "${NP2_ROOT}/calendar.c"
    "${NP2_ROOT}/debugsnapshot.c"
    "${NP2_ROOT}/keystat.c"
    "${NP2_ROOT}/nevent.c"
    "${NP2_ROOT}/pccore.c"
    "${NP2_ROOT}/statsave.c"
    "${NP2_ROOT}/timing.c")
file(GLOB NP2_COMMON_SOURCES_TMP
    "${NP2_ROOT}/bios/*.c"
    "${NP2_ROOT}/cbus/*.c"
    "${NP2_ROOT}/common/*.c"
    "${NP2_ROOT}/diskimage/*.c"
    "${NP2_ROOT}/diskimage/fd/*.c"
    "${NP2_ROOT}/fdd/*.c"
    "${NP2_ROOT}/font/*.c"
    "${NP2_ROOT}/io/*.c"
    "${NP2_ROOT}/io/pci/*.c"
    "${NP2_ROOT}/lio/*.c"
    "${NP2_ROOT}/mem/*.c"
    "${NP2_ROOT}/sound/getsnd/*.c"
    "${NP2_ROOT}/sound/mame/*.c"
    "${NP2_ROOT}/sound/vermouth/*.c"
    "${NP2_ROOT}/trap/*.c"
    "${NP2_ROOT}/vram/*.c"
    "${NP2_ROOT}/wab/*.c")
list(APPEND NP2_COMMON_SOURCES ${NP2_COMMON_SOURCES_TMP})

file(GLOB NP2_COMMON_SOURCES_TMP
    "${NP2_ROOT}/codecnv/eucsjis.c"
    "${NP2_ROOT}/codecnv/eucucs2.c"
    "${NP2_ROOT}/codecnv/jisucs2.c"
    "${NP2_ROOT}/codecnv/sjiseuc.c"
    "${NP2_ROOT}/codecnv/sjisucs2.c"
    "${NP2_ROOT}/codecnv/tcswap16.c"
    "${NP2_ROOT}/codecnv/tcswap32.c"
    "${NP2_ROOT}/codecnv/ucs2sjis.c"
    "${NP2_ROOT}/codecnv/ucs2ucs4.c"
    "${NP2_ROOT}/codecnv/ucs2utf8.c"
    "${NP2_ROOT}/codecnv/ucs4ucs2.c"
    "${NP2_ROOT}/codecnv/ucs4utf8.c"
    "${NP2_ROOT}/codecnv/utf8ucs2.c"
    "${NP2_ROOT}/codecnv/utf8ucs4.c"
    "${NP2_ROOT}/diskimage/cd/cdd_ccd.c"
    "${NP2_ROOT}/diskimage/cd/cdd_cue.c"
    "${NP2_ROOT}/diskimage/cd/cdd_iso.c"
    "${NP2_ROOT}/diskimage/cd/cdd_mds.c"
    "${NP2_ROOT}/diskimage/cd/cdd_nrg.c"
    "${NP2_ROOT}/generic/cmjasts.c"
    "${NP2_ROOT}/generic/cmndraw.c"
    "${NP2_ROOT}/generic/cmver.c"
    "${NP2_ROOT}/generic/dipswbmp.c"
    "${NP2_ROOT}/generic/hostdrvs.c"
    "${NP2_ROOT}/generic/keydisp.c"
    "${NP2_ROOT}/generic/memdbg32.c"
    "${NP2_ROOT}/generic/np2info.c"
    "${NP2_ROOT}/generic/softkbd.c"
    "${NP2_ROOT}/generic/unasm.c"
    "${NP2_ROOT}/sound/adpcmc.c"
    "${NP2_ROOT}/sound/adpcmg.c"
    "${NP2_ROOT}/sound/beepc.c"
    "${NP2_ROOT}/sound/beepg.c"
    "${NP2_ROOT}/sound/cs4231c.c"
    "${NP2_ROOT}/sound/cs4231g.c"
    "${NP2_ROOT}/sound/ct1741c.c"
    "${NP2_ROOT}/sound/ct1741g.c"
    "${NP2_ROOT}/sound/fmboard.c"
    "${NP2_ROOT}/sound/oplgenc.c"
    "${NP2_ROOT}/sound/oplgeng.c"
    "${NP2_ROOT}/sound/opngenc.c"
    "${NP2_ROOT}/sound/opngeng.c"
    "${NP2_ROOT}/sound/opntimer.c"
    "${NP2_ROOT}/sound/pcm86c.c"
    "${NP2_ROOT}/sound/pcm86g.c"
    "${NP2_ROOT}/sound/pcmmix.c"
    "${NP2_ROOT}/sound/psggenc.c"
    "${NP2_ROOT}/sound/psggeng.c"
    "${NP2_ROOT}/sound/rhythmc.c"
    "${NP2_ROOT}/sound/s98.c"
    "${NP2_ROOT}/sound/sndcsec.c"
    "${NP2_ROOT}/sound/sound.c"
    "${NP2_ROOT}/sound/soundrom.c"
    "${NP2_ROOT}/sound/tms3631c.c"
    "${NP2_ROOT}/sound/tms3631g.c")
list(APPEND NP2_COMMON_SOURCES ${NP2_COMMON_SOURCES_TMP})

file(GLOB NP2_COMMON_SOURCES_TMP
    "${NP2_ROOT}/sound/fmgen/*.c"
    "${NP2_ROOT}/sound/fmgen/*.cpp")
list(APPEND NP2_COMMON_SOURCES ${NP2_COMMON_SOURCES_TMP})

file(GLOB NP2_COMMON_SOURCES_TMP "${NP2_ROOT}/sound/mamebsd/*.cpp")
list(APPEND NP2_COMMON_SOURCES ${NP2_COMMON_SOURCES_TMP})

# i386 / IA32 CPU core (PC-9821 class machines).
file(GLOB NP2_IA32_SOURCES
    "${NP2_ROOT}/debugsub386.c"
    "${NP2_ROOT}/generic/hostdrv.c")
file(GLOB NP2_IA32_SOURCES_TMP
    "${NP2_ROOT}/i386c/*.c"
    "${NP2_ROOT}/i386c/ia32/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/softfloat/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/softfloat3/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/mmx/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/sse/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/sse2/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/sse3/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/ssse3/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/sse4/*.c"
    "${NP2_ROOT}/i386c/ia32/instructions/sse4a/*.c")
list(APPEND NP2_IA32_SOURCES ${NP2_IA32_SOURCES_TMP})
list(APPEND NP2_IA32_SOURCES
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/fpdummy.c"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/fpemul_dosbox.c"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/fpemul_dosbox2.c"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/fpemul_softfloat.c"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/fpemul_softfloat3.cpp")

# Platform layer: the shared SDL/port glue plus the Libretro front-end.
file(GLOB NP2_SDL_SOURCES
    "${NP2_ROOT}/sdl/*.c"
    "${NP2_ROOT}/embed/*.c"
    "${NP2_ROOT}/embed/menu/*.c"
    "${NP2_ROOT}/embed/menubase/*.c")
list(APPEND NP2_SDL_SOURCES
    "${NP2_ROOT}/codecnv/textcnv.c"
    "${NP2_ROOT}/sound/opl3.c"
    "${NP2_ROOT}/sound/opna.c"
    "${NP2_ROOT}/sdl/libretro/libretro.c")

file(GLOB NP2_THREAD_SOURCES
    "${NP2_ROOT}/np2_tickcount.c")

# ---------------------------------------------------------------------------
# libretro-common: the support library the Libretro front-end relies on
# (filestream/VFS, dirent, string helpers, UTF-8, CPU feature detection).
# This is the exact non-static set NP2kai's Makefile.libretro builds, minus
# the image/archive decoders the emulator core never calls and minus rthreads
# (NP2kai only needs it for SUPPORT_NP2_THREAD, which the Asuka build leaves
# off so the web build stays free of pthreads). Listed explicitly rather than
# globbed because the directories also hold platform variants (nbio_orbis,
# vfs_implementation_cdrom, ...) that upstream selects per platform.
# ---------------------------------------------------------------------------
set(NP2_LC_SOURCES
    "${NP2_LC}/compat/compat_strcasestr.c"
    "${NP2_LC}/compat/compat_fnmatch.c"
    "${NP2_LC}/compat/compat_getopt.c"
    "${NP2_LC}/compat/compat_posix_string.c"
    "${NP2_LC}/compat/compat_snprintf.c"
    "${NP2_LC}/compat/compat_strl.c"
    "${NP2_LC}/compat/compat_vscprintf.c"
    "${NP2_LC}/compat/fopen_utf8.c"
    "${NP2_LC}/encodings/encoding_crc32.c"
    "${NP2_LC}/encodings/encoding_utf.c"
    "${NP2_LC}/features/features_cpu.c"
    "${NP2_LC}/file/config_file.c"
    "${NP2_LC}/file/config_file_userdata.c"
    "${NP2_LC}/file/file_path.c"
    "${NP2_LC}/file/file_path_io.c"
    "${NP2_LC}/file/retro_dirent.c"
    "${NP2_LC}/file/nbio/nbio_intf.c"
    "${NP2_LC}/file/nbio/nbio_stdio.c"
    "${NP2_LC}/lists/dir_list.c"
    "${NP2_LC}/lists/file_list.c"
    "${NP2_LC}/lists/string_list.c"
    "${NP2_LC}/lists/vector_list.c"
    "${NP2_LC}/streams/file_stream.c"
    "${NP2_LC}/streams/file_stream_transforms.c"
    "${NP2_LC}/streams/interface_stream.c"
    "${NP2_LC}/streams/memory_stream.c"
    "${NP2_LC}/streams/stdin_stream.c"
    "${NP2_LC}/streams/trans_stream.c"
    "${NP2_LC}/streams/trans_stream_pipe.c"
    "${NP2_LC}/string/stdstring.c"
    "${NP2_LC}/vfs/vfs_implementation.c")

# The Unix memory-mapped nbio backend, matching Makefile.libretro's
# platform==unix selection.
if(UNIX AND NOT APPLE)
    list(APPEND NP2_LC_SOURCES "${NP2_LC}/file/nbio/nbio_linux.c")
endif()

# ---------------------------------------------------------------------------
# Engine core (static)
# ---------------------------------------------------------------------------
add_library(asuka_pc98_core STATIC
    PC98Engine.cpp
    ${NP2_COMMON_SOURCES}
    ${NP2_IA32_SOURCES}
    ${NP2_SDL_SOURCES}
    ${NP2_THREAD_SOURCES}
    ${NP2_LC_SOURCES})

target_include_directories(asuka_pc98_core PUBLIC
    "${CMAKE_SOURCE_DIR}/include")
target_include_directories(asuka_pc98_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")

# Vendor headers are treated as system headers so their diagnostics stay out
# of our glue compilation.
target_include_directories(asuka_pc98_core SYSTEM PRIVATE
    "${NP2_ROOT}"
    "${NP2_ROOT}/sdl"
    "${NP2_ROOT}/sdl/libretro"
    "${NP2_LC}/include"
    "${NP2_LC}/include/array"
    "${NP2_LC}/include/audio"
    "${NP2_LC}/include/audio/conversion"
    "${NP2_LC}/include/compat"
    "${NP2_LC}/include/dynamic"
    "${NP2_LC}/include/encodings"
    "${NP2_LC}/include/features"
    "${NP2_LC}/include/file"
    "${NP2_LC}/include/formats"
    "${NP2_LC}/include/lists"
    "${NP2_LC}/include/math"
    "${NP2_LC}/include/queues"
    "${NP2_LC}/include/rthreads"
    "${NP2_LC}/include/streams"
    "${NP2_LC}/include/string"
    "${NP2_LC}/include/utils"
    "${NP2_LC}/include/vfs"
    "${NP2_ROOT}/cbus"
    "${NP2_ROOT}/common"
    "${NP2_ROOT}/diskimage"
    "${NP2_ROOT}/diskimage/cd"
    "${NP2_ROOT}/diskimage/fd"
    "${NP2_ROOT}/embed"
    "${NP2_ROOT}/embed/menu"
    "${NP2_ROOT}/embed/menubase"
    "${NP2_ROOT}/fdd"
    "${NP2_ROOT}/font"
    "${NP2_ROOT}/generic"
    "${NP2_ROOT}/io"
    "${NP2_ROOT}/io/pci"
    "${NP2_ROOT}/lio"
    "${NP2_ROOT}/mem"
    "${NP2_ROOT}/sound"
    "${NP2_ROOT}/sound/vermouth"
    "${NP2_ROOT}/sound/mame"
    "${NP2_ROOT}/sound/mamebsd"
    "${NP2_ROOT}/sound/fmgen"
    "${NP2_ROOT}/trap"
    "${NP2_ROOT}/vram"
    "${NP2_ROOT}/wab"
    "${NP2_ROOT}/i386c"
    "${NP2_ROOT}/i386c/ia32"
    "${NP2_ROOT}/i386c/ia32/instructions"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/softfloat"
    "${NP2_ROOT}/i386c/ia32/instructions/fpu/softfloat3"
    "${NP2_ROOT}/i386c/ia32/instructions/mmx"
    "${NP2_ROOT}/i386c/ia32/instructions/sse"
    "${NP2_ROOT}/i386c/ia32/instructions/sse2"
    "${NP2_ROOT}/i386c/ia32/instructions/sse3"
    "${NP2_ROOT}/i386c/ia32/instructions/ssse3"
    "${NP2_ROOT}/i386c/ia32/instructions/sse4"
    "${NP2_ROOT}/i386c/ia32/instructions/sse4a")

# The definition set matches NP2kai's own Libretro build (Makefile.common +
# Makefile.libretro). SUPPORT_PNG is deliberately omitted: it would drag in
# libpng for the (unused) screenshot encoder, which the headless core never
# calls.
target_compile_definitions(asuka_pc98_core PRIVATE
    __LIBRETRO__
    NP2KAI_GIT_TAG="asuka"
    NP2KAI_GIT_HASH="vendored"
    # --- NP2DEFINE (common) ---
    BIOS_IO_EMULATION
    OSLANG_UTF8
    OSLINEBREAK_LF
    SUPPORT_8BPP
    SUPPORT_16BPP
    SUPPORT_24BPP
    SUPPORT_32BPP
    SUPPORT_CL_GD5430
    SUPPORT_CRT15KHZ
    SUPPORT_FAST_MEMORYCHECK
    SUPPORT_FMGEN
    SUPPORT_GPIB
    SUPPORT_HRTIMER
    SUPPORT_KAI_IMAGES
    SUPPORT_LARGE_HDD
    SUPPORT_NORMALDISP
    SUPPORT_PC9861K
    SUPPORT_PX
    SUPPORT_RESUME
    SUPPORT_RS232C_FIFO
    SUPPORT_S98
    SUPPORT_SCSI
    SUPPORT_SMPU98
    SUPPORT_SOFTKBD=0
    SUPPORT_SOUND_SB16
    SUPPORT_STATSAVE=10
    SUPPORT_UTF8
    SUPPORT_V30EXT
    SUPPORT_V30ORIGINAL
    SUPPORT_VIDEOFILTER
    SUPPORT_VPCVHD
    SUPPORT_WAB
    USE_MAME
    USE_MAME_BSD
    VAEG_FIX
    VERMOUTH_LIB
    # --- NP21DEFINE (IA32 / PC-9821) ---
    CPUCORE_IA32
    IA32_PAGING_EACHSIZE
    IA32_REBOOT_ON_PANIC
    SUPPORT_CRT31KHZ
    SUPPORT_PC9801_119
    SUPPORT_PC9821
    SUPPORT_IDEIO
    SUPPORT_LARGE_MEMORY
    SUPPORT_FPU_DOSBOX2_COMPATIBLE
    SUPPORT_PEGC
    SUPPORT_VGA_MODEX
    SUPPORT_GAMEPORT
    SUPPORT_IDEIO_48BIT
    SUPPORT_FPU_SOFTFLOAT3
    SUPPORT_PCI
    USE_FPU
    USE_MMX
    USE_3DNOW
    USE_SSE
    USE_SSE2
    USE_SSE3
    USE_SSSE3
    USE_SSE4_1
    USE_SSE4_2
    USE_SSE4A
    USE_TSC
    USE_FASTPAGING
    USE_VME
    SUPPORT_MEMDBG32
    SUPPORT_HOSTDRV
    # --- timing helpers ---
    SUPPORT_NP2_TICKCOUNT)

target_link_libraries(asuka_pc98_core PRIVATE asuka_warnings)

# Keep the engine internals out of the plugin's dynamic symbol table; only the
# ASUKA_EXPORT factory entry points stay visible.
set_target_properties(asuka_pc98_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Silence warnings for vendored code only (appended after target options).
set_source_files_properties(
    ${NP2_COMMON_SOURCES} ${NP2_IA32_SOURCES} ${NP2_SDL_SOURCES}
    ${NP2_THREAD_SOURCES} ${NP2_LC_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-w")

# NP2kai's C sources rely on implicit declarations and incompatible function
# pointer casts; GCC 14 promotes those to errors, so relax them exactly as
# NP2kai's own Makefile.common does (-fno-sized-deallocation likewise, for the
# C++ sources).
target_compile_options(asuka_pc98_core PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>
    $<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-function-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wno-int-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-sized-deallocation>)

# NP2kai's headers rely on compiler.h being seen first for their integer
# typedefs; some vendored C files also fail if it is not. Force it in for the C
# translation units, resolved through the include path exactly as NP2kai's own
# build does (the glue file includes it explicitly, first, itself).
target_compile_options(asuka_pc98_core PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include;compiler.h>)

# ymfm (the MAME BSD FM sound core) calls std::abs() without including
# <cmath>; force the header in for the C++ translation units only.
target_compile_options(asuka_pc98_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include;cmath>)

# --- Desktop plugin (shared) -------------------------------------------------
# Native only: the web build links asuka_pc98_static instead.
if(NOT ASUKA_BUILD_WEB)
    add_library(asuka_pc98 SHARED
        pc98_plugin.cpp)

    target_compile_definitions(asuka_pc98 PRIVATE ASUKA_PLUGIN_SHARED)

    target_link_libraries(asuka_pc98 PRIVATE asuka_pc98_core asuka_warnings)

    set_target_properties(asuka_pc98 PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/pc98"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/pc98")
endif()

# --- Static plugin (web) -----------------------------------------------------
# The web build links the engine in directly instead of dlopen-ing a module.
# The generic create_engine/destroy_engine names are not exported here: the FM
# Towns engine is linked into the same module and owns those.
if(ASUKA_BUILD_WEB)
    add_library(asuka_pc98_static STATIC
        pc98_plugin.cpp)
    target_link_libraries(asuka_pc98_static PRIVATE
        asuka_pc98_core asuka_warnings)
endif()
