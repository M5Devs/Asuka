#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "asuka/IEngineCore.hpp"

namespace asuka::plugins::x68k {

// Asuka engine core backed by the PX68k Sharp X68000 emulator.
//
// PX68k's Libretro front-end (libretro.c) is the only headless, pull-model
// entry point, so that is what this wrapper drives, exactly as the PC-98
// plugin drives NP2kai's. The Libretro contract already matches the Asuka ABI
// closely, so the adapter is mostly a translation of callbacks:
//
//   * the host plays the role of a Libretro frontend - it answers the
//     environment callback, and captures the video/audio the core pushes
//     during retro_run();
//   * retro_run() advances the machine exactly one frame and emits one
//     framebuffer plus one audio batch, which is precisely the
//     step_frame() / get_framebuffer() / get_audio_samples() contract.
//
// The core renders 16-bit RGB565; this adapter expands every frame to the
// XRGB8888 layout IEngineCore expects.
//
// Media types supported by PX68k: floppy images (.dim .xdf .d88 .fdi ...),
// hard disk images (.hds .hdf ...) and a disk-set playlist (.m3u).
//
// Unlike NP2kai, PX68k has no built-in BIOS fallback: it needs the X68000 IPL
// ROM (iplrom.dat / iplrom30.dat / iplromco.dat / iplromxv.dat) and the
// character-generator ROM (cgrom.dat) in its system directory. init() still
// succeeds without them (the load simply fails later, reported through the
// log), matching the other cores' "graceful, never crash" behaviour.
class X68kEngine final : public IEngineCore {
public:
    X68kEngine();
    ~X68kEngine() override;

    X68kEngine(const X68kEngine&) = delete;
    X68kEngine& operator=(const X68kEngine&) = delete;

    // `system_path` is unused; `bios_path` points at the directory holding the
    // X68000 ROM set (iplrom.dat, cgrom.dat, ...). The core reads firmware
    // relative to the directory it is given through the environment callback.
    bool init(const std::string& system_path,
              const std::string& bios_path) override;
    bool load_media(const std::string& media_path) override;
    void step_frame() override;
    const uint32_t* get_framebuffer(int& width, int& height) override;
    const int16_t* get_audio_samples(size_t& sample_count) override;
    void send_input(uint32_t key_code, bool pressed) override;

    // Feed a raw keyboard code. PX68k polls the keyboard through Libretro's
    // RETRO_DEVICE_KEYBOARD, whose identifiers are the RetroKey/ SDLK_* values;
    // `scancode` is such a value, letting the shell's soft keyboard reach games
    // that are keyboard-driven (most X68000 titles). Cores that cannot express
    // a key through asuka::Key use this path.
    void send_scancode(int scancode, bool pressed) override;

    bool save_snapshot(const std::string& filepath) override;
    bool load_snapshot(const std::string& filepath) override;
    void reset() override;
    void shutdown() override;

    // Libretro callbacks: the core pushes one frame and one audio batch during
    // retro_run(), which these capture for the getters above. They are public
    // only so the file-scope Libretro trampolines can reach them.
    void capture_frame(const void* data, unsigned width, unsigned height,
                       size_t pitch);
    void capture_audio(const int16_t* data, size_t frames);

private:
    // Run the one-off Libretro bootstrap (the first retro_run() call performs
    // pmain, which powers the machine on and mounts any media). Called lazily
    // by step_frame() and by the snapshot paths.
    void prime();

    // Point the core's firmware directory at the directory the host supplied.
    // The core builds it as "<system_dir>/keropi"; this rewrites it to the
    // actual BIOS directory so iplrom.dat/cgrom.dat resolve.
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

}  // namespace asuka::plugins::x68k
