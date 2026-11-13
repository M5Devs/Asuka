#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "asuka/IEngineCore.hpp"

// Forward declaration keeps the Geartowns headers out of our public interface.
class GeartownsCore;

namespace asuka::plugins::fmtowns {

// Asuka engine core backed by the Geartowns FM Towns / Marty emulator.
//
// Geartowns owns its own video and audio generation: a single call to
// RunToFrame() advances the machine by one frame and fills an XRGB8888
// framebuffer plus an interleaved 16-bit stereo PCM buffer. This wrapper
// adapts that pull-oriented, per-frame model to the Asuka IEngineCore ABI.
//
// Media types supported by Geartowns: CHD, CUE/BIN, ISO and floppy images.
class FMTownsEngine final : public IEngineCore {
public:
    FMTownsEngine();
    ~FMTownsEngine() override;

    FMTownsEngine(const FMTownsEngine&) = delete;
    FMTownsEngine& operator=(const FMTownsEngine&) = delete;

    // `system_path` is ignored for FM Towns; `bios_path` points at the folder
    // holding FMT_SYS.ROM, FMT_DOS.ROM, FMT_FNT.ROM, FMT_DIC.ROM, FMT_F20.ROM.
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

    // Optional knobs, used by the CLI before init()/load_media().

    // Force a specific machine model (a GT_Machine_Model cast to int, or -1
    // for automatic selection). Default: automatic.
    void set_machine_model(int model);
    // Install the FM Towns OS to boot from an internal RAM disk instead of
    // the boot ROM (a.k.a. "three mode" / DOS on CD). Default: false.
    void set_boot_from_cd(bool enabled);

private:
    // GeartownsCore is declared here so the C++ type is known for unique_ptr;
    // the definition is only needed in the .cpp.
    struct CoreDeleter {
        void operator()(GeartownsCore* core) const noexcept;
    };

    // Write the current frame as a 24-bit BMP next to a state file. Returns
    // false if there is no frame yet or the file cannot be written.
    bool write_thumbnail_bmp(const std::string& state_path) const;

    std::unique_ptr<GeartownsCore, CoreDeleter> core_;
    bool initialized_ = false;
    int machine_model_ = -1;  // GT_MACHINE_AUTO
    bool boot_from_cd_ = false;

    // Latched Game Port 1 (Towns Pad) button state, mirrored to the core on
    // every send_input(). Geartowns stores the pad as a level, not an edge, so
    // the host has to track which buttons are held.
    uint16_t gamepad_buttons_ = 0;

    // Owned render/audio targets handed to Geartowns each frame.
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;

    int frame_width_ = 0;
    int frame_height_ = 0;
    size_t audio_samples_per_channel_ = 0;
};

}  // namespace asuka::plugins::fmtowns
