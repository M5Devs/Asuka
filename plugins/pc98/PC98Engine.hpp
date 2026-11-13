#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "asuka/IEngineCore.hpp"

namespace asuka::plugins::pc98 {

// Asuka engine core backed by the NP2kai NEC PC-9801 / PC-9821 emulator.
//
// NP2kai ships several front-ends; this wrapper drives its Libretro core
// (sdl/libretro/libretro.c, built with -D__LIBRETRO__), which is the only
// headless, pull-model entry point. The Libretro contract already matches the
// Asuka ABI closely, so the adapter is mostly a translation of callbacks:
//
//   * the host plays the role of a Libretro frontend - it answers the
//     environment callback, and captures the video/audio the core pushes
//     during retro_run();
//   * retro_run() advances the machine exactly one frame and emits one
//     framebuffer plus one audio batch, which is precisely the
//     step_frame() / get_framebuffer() / get_audio_samples() contract.
//
// The core renders 32-bit XRGB8888 (its RGB32 union is 0x00RRGGBB in
// little-endian memory), which is the layout IEngineCore expects.
//
// Media types supported by NP2kai: floppy images (.d88 .fdi .hdm .nfd ...),
// hard disk images (.hdi .nhd .vhd ...) and CD images (.cue .iso .ccd .m3u).
class PC98Engine final : public IEngineCore {
public:
    PC98Engine();
    ~PC98Engine() override;

    PC98Engine(const PC98Engine&) = delete;
    PC98Engine& operator=(const PC98Engine&) = delete;

    // `system_path` is optional and may hold auxiliary assets (a font file);
    // `bios_path` points at the directory holding the PC-98 ROM set
    // (bios.rom, itf.rom, font.bmp). NP2kai can start without a BIOS ROM by
    // falling back to its built-in BIOS I/O emulation, so a missing BIOS is
    // reported but is not fatal.
    bool init(const std::string& system_path,
              const std::string& bios_path) override;
    bool load_media(const std::string& media_path) override;
    void step_frame() override;
    const uint32_t* get_framebuffer(int& width, int& height) override;
    const int16_t* get_audio_samples(size_t& sample_count) override;
    void send_input(uint32_t key_code, bool pressed) override;
    bool save_snapshot(const std::string& filepath) override;
    bool load_snapshot(const std::string& filepath) override;
    void reset() override;
    void shutdown() override;

    // Feed a native PC-98 keyboard make code directly (0x00-based, as listed
    // by NP2kai's scan-code table). The web shell's on-screen PC-98 keyboard
    // uses this to reach keys the generic asuka::Key set cannot express
    // (HELP, STOP, ROLL UP/DOWN, F1-F5, letters, ...). `pressed` is true on
    // key-down and false on key-up.
    void send_scancode(int scancode, bool pressed) override;

    // Libretro callbacks: the core pushes one frame and one audio batch during
    // retro_run(), which these capture for the getters above. They are public
    // only so the file-scope Libretro trampolines can reach them.
    void capture_frame(const void* data, unsigned width, unsigned height,
                       size_t pitch);
    void capture_audio(const int16_t* data, size_t frames);

private:
    // Run the one-off Libretro bootstrap (retro_run -> np2_main) that powers
    // the machine on and mounts any media. Called lazily by step_frame().
    void prime();

    // Point the core's firmware directory at the host-supplied BIOS path
    // (retro_load_game() defaults it to "<system_dir>/np2kai").
    void apply_bios_path();

    // Write the current frame as a 24-bit BMP next to a state file.
    bool write_thumbnail_bmp(const std::string& state_path) const;

    bool initialized_ = false;
    bool primed_ = false;
    bool game_loaded_ = false;

    std::string system_path_;
    std::string bios_path_;
    std::string media_path_;

    // Current frame, copied out of the core's own buffer on every step so the
    // pointer handed to get_framebuffer() stays valid and the thumbnail writer
    // has a stable source.
    std::vector<uint32_t> framebuffer_;
    int frame_width_ = 0;
    int frame_height_ = 0;

    // Interleaved stereo PCM accumulated during the last step_frame().
    std::vector<int16_t> audio_;
    size_t audio_samples_per_channel_ = 0;
};

}  // namespace asuka::plugins::pc98
