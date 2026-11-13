# FM Towns / Marty engine plugin, wrapping the Geartowns core.
#
# Only Geartowns' emulation core and its data dependencies are built here: the
# desktop GUI stack (SDL3, OpenGL, imgui, glad) is not needed for a headless
# Asuka plugin.

find_package(Threads REQUIRED)

set(GT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Geartowns")
set(GT_SRC "${GT_ROOT}/src")
set(GT_DEPS "${GT_ROOT}/platforms/shared/dependencies")

if(NOT EXISTS "${GT_SRC}/geartowns_core.cpp")
    message(FATAL_ERROR
        "Geartowns sources are missing. Fetch the submodule with:\n"
        "  git submodule update --init --recursive")
endif()

# --- Geartowns emulation core ------------------------------------------------
set(GT_CORE_SOURCES
    "${GT_SRC}/geartowns_core.cpp"
    "${GT_SRC}/common/profiler.cpp"
    "${GT_SRC}/common/trace_logger.cpp"
    "${GT_SRC}/audio/audio.cpp"
    "${GT_SRC}/audio/pcm16.cpp"
    "${GT_SRC}/audio/rf5c68.cpp"
    "${GT_SRC}/audio/ym3438.cpp"
    "${GT_SRC}/audio/vgm_recorder.cpp"
    "${GT_SRC}/cdrom/cdrom.cpp"
    "${GT_SRC}/cdrom/cdrom_audio.cpp"
    "${GT_SRC}/cdrom/cdrom_chd_image.cpp"
    "${GT_SRC}/cdrom/cdrom_cuebin_image.cpp"
    "${GT_SRC}/cdrom/cdrom_image.cpp"
    "${GT_SRC}/cdrom/cdrom_media.cpp"
    "${GT_SRC}/drive/fdc.cpp"
    "${GT_SRC}/drive/mb8877.cpp"
    "${GT_SRC}/drive/scsi_controller.cpp"
    "${GT_SRC}/drive/scsi_disk.cpp"
    "${GT_SRC}/drive/hard_disk_image.cpp"
    "${GT_SRC}/drive/floppy_disk.cpp"
    "${GT_SRC}/drive/floppy_image.cpp"
    "${GT_SRC}/drive/memory_card.cpp"
    "${GT_SRC}/i386/i386.cpp"
    "${GT_SRC}/i386/i386_486.cpp"
    "${GT_SRC}/i386/i386_bus.cpp"
    "${GT_SRC}/i386/i386_disassembler.cpp"
    "${GT_SRC}/i386/i386_dynarec.cpp"
    "${GT_SRC}/i386/i386_dynarec_emit.cpp"
    "${GT_SRC}/i386/i386_encoding.cpp"
    "${GT_SRC}/i386/i386_functors.cpp"
    "${GT_SRC}/i386/i386_memory.cpp"
    "${GT_SRC}/i386/i386_opcodes.cpp"
    "${GT_SRC}/i386/i386_opcodes_0f.cpp"
    "${GT_SRC}/i386/i386_opcodes_groups.cpp"
    "${GT_SRC}/i386/i386_opcodes_string.cpp"
    "${GT_SRC}/i386/i386_opcodes_transfer.cpp"
    "${GT_SRC}/i386/i386_protected.cpp"
    "${GT_SRC}/i386/i386_protected_transfer.cpp"
    "${GT_SRC}/i386/i386_task.cpp"
    "${GT_SRC}/i386/exec_memory.cpp"
    "${GT_SRC}/i386/x87.cpp"
    "${GT_SRC}/input/input.cpp"
    "${GT_SRC}/input/keyboard.cpp"
    "${GT_SRC}/media/chd_file_adapter.cpp"
    "${GT_SRC}/media/firmware.cpp"
    "${GT_SRC}/media/media.cpp"
    "${GT_SRC}/media/media_file.cpp"
    "${GT_SRC}/media/media_file_native.cpp"
    "${GT_SRC}/media/ogg_vorbis_decoder.cpp"
    "${GT_SRC}/system/i8253.cpp"
    "${GT_SRC}/system/i8259.cpp"
    "${GT_SRC}/system/io.cpp"
    "${GT_SRC}/system/memory.cpp"
    "${GT_SRC}/system/pic.cpp"
    "${GT_SRC}/system/pit.cpp"
    "${GT_SRC}/system/msm58321.cpp"
    "${GT_SRC}/system/scheduler.cpp"
    "${GT_SRC}/system/system_control.cpp"
    "${GT_SRC}/system/upd71071.cpp"
    "${GT_SRC}/system/load_detector.cpp"
    "${GT_SRC}/video/hires_crtc.cpp"
    "${GT_SRC}/video/sprite.cpp"
    "${GT_SRC}/video/video.cpp"
    "${GT_DEPS}/bochs_fpu/bochs_fpu.cpp")

# --- Third-party data dependencies -------------------------------------------
set(GT_DEP_SOURCES
    "${GT_DEPS}/libchdr/src/libchdr_bitstream.c"
    "${GT_DEPS}/libchdr/src/libchdr_cdrom.c"
    "${GT_DEPS}/libchdr/src/libchdr_chd.c"
    "${GT_DEPS}/libchdr/src/libchdr_flac.c"
    "${GT_DEPS}/libchdr/src/libchdr_huffman.c"
    "${GT_DEPS}/lzma/src/LzFind.c"
    "${GT_DEPS}/lzma/src/LzmaEnc.c"
    "${GT_DEPS}/lzma/src/LzmaDec.c"
    "${GT_DEPS}/lzma/src/CpuArch.c"
    "${GT_DEPS}/minivorbis/minivorbis.c"
    "${GT_DEPS}/miniz/miniz.c")

add_library(asuka_fmtowns SHARED
    FMTownsEngine.cpp
    fmtowns_plugin.cpp
    gt_compat.cpp
    ${GT_CORE_SOURCES}
    ${GT_DEP_SOURCES})

# Our own glue sources compile with the shared strict warning set; the
# vendored Geartowns and dependency sources get warning suppression below.
target_link_libraries(asuka_fmtowns PRIVATE asuka_warnings Threads::Threads)

# Vendored code references dlopen()/dlsym() (Linux), so link libdl where needed.
if(UNIX AND NOT APPLE)
    target_link_libraries(asuka_fmtowns PRIVATE ${CMAKE_DL_LIBS})
endif()

# Vendor headers are treated as system headers so their diagnostics stay out
# of our glue compilation.
target_include_directories(asuka_fmtowns SYSTEM PRIVATE
    "${GT_SRC}"
    "${GT_DEPS}/bochs_fpu"
    "${GT_DEPS}/json"
    "${GT_DEPS}/libchdr/include"
    "${GT_DEPS}/lzma/include"
    "${GT_DEPS}/minivorbis"
    "${GT_DEPS}/miniz"
    "${GT_DEPS}/zstd")

target_include_directories(asuka_fmtowns PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(asuka_fmtowns PRIVATE
    # Interpreter instead of the Lightning JIT: no extra dependency and fully
    # deterministic, which suits a headless runner.
    GT_DISABLE_DYNAREC
    # Match Geartowns' own dependency build flags.
    Z7_ST
    ZSTD_DISABLE_ASM
    EMULATOR_BUILD="asuka")

# Geartowns builds its core without exceptions or RTTI in release.
set_target_properties(asuka_fmtowns PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/fmtowns"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/fmtowns")

# Silence warnings for vendored code only (appended after target options).
set_source_files_properties(
    ${GT_CORE_SOURCES} ${GT_DEP_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-w")
