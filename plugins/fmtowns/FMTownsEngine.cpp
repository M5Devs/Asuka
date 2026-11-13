#include "FMTownsEngine.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "geartowns.h"

namespace asuka::plugins::fmtowns {

namespace {

// Geartowns' maximum render surface; the actual frame is usually smaller.
constexpr int kMaxWidth = GT_MAX_FRAME_BUFFER_WIDTH;    // 1024
constexpr int kMaxHeight = GT_MAX_FRAME_BUFFER_HEIGHT;  // 768
constexpr int kFrameRate = GT_VIDEO_FRAME_RATE;         // 60
constexpr int kSampleRate = GT_AUDIO_SAMPLE_RATE;       // 44100

// Audio buffer sized for the highest per-frame sample count plus headroom.
constexpr size_t kMaxAudioValues =
    2u * static_cast<size_t>((kSampleRate + kFrameRate - 1) / kFrameRate + 512);

}  // namespace

FMTownsEngine::FMTownsEngine() = default;

FMTownsEngine::~FMTownsEngine() { shutdown(); }

void FMTownsEngine::CoreDeleter::operator()(GeartownsCore* core) const noexcept {
    delete core;
}

bool FMTownsEngine::init(const std::string& system_path,
                         const std::string& bios_path) {
    // `system_path` is not used: FM Towns media carry no runtime asset tree.
    (void)system_path;

    if (initialized_) {
        return true;
    }

    core_ = std::unique_ptr<GeartownsCore, CoreDeleter>(new GeartownsCore());
    if (!core_) {
        return false;
    }
    core_->Init();

    framebuffer_.assign(static_cast<size_t>(kMaxWidth) * kMaxHeight, 0xFF000000u);
    audio_.assign(kMaxAudioValues, 0);

    initialized_ = true;

    // BIOS/firmware is mandatory for a powered-on machine. Loading it here
    // means init() reports a clear failure when the ROM set is missing.
    if (!bios_path.empty()) {
        if (!core_->LoadBios(bios_path.c_str())) {
            std::fprintf(stderr,
                         "asuka/fmtowns: failed to load FM Towns BIOS from '%s'\n",
                         bios_path.c_str());
            return false;
        }
    } else {
        std::fprintf(stderr,
                     "asuka/fmtowns: no BIOS directory supplied; power-on will "
                     "fail until firmware is available\n");
        return false;
    }

    // Select the machine (automatic unless overridden), then cold-boot.
    GT_Machine_Settings settings;
    settings.model = machine_model_;
    settings.cpu_mhz = GT_MACHINE_AUTO;
    settings.ram_mb = GT_MACHINE_AUTO;
    settings.fpu = GT_MACHINE_AUTO;
    settings.speed_mode = GT_MACHINE_AUTO;
    core_->SetMachineSettings(settings);

    if (boot_from_cd_) {
        core_->SetBootDevice(GT_BOOT_DEVICE_CDROM);
    }

    if (!core_->PowerOn()) {
        std::fprintf(stderr, "asuka/fmtowns: PowerOn() failed (firmware set "
                             "incomplete?)\n");
        return false;
    }
    return true;
}

bool FMTownsEngine::load_media(const std::string& media_path) {
    if (!initialized_ || media_path.empty()) {
        return false;
    }
    if (!core_->LoadMedia(media_path.c_str())) {
        std::fprintf(stderr, "asuka/fmtowns: cannot load media '%s'\n",
                     media_path.c_str());
        return false;
    }
    // Let the machine pick up the new disc: re-read firmware/media references
    // and cold-reset so the boot sequence starts from the new medium.
    core_->LatchAutoMedia();
    core_->ResetMedia();
    return true;
}

void FMTownsEngine::step_frame() {
    if (!initialized_) {
        return;
    }

    int sample_count = 0;
    core_->RunToFrame(reinterpret_cast<u8*>(framebuffer_.data()),
                      audio_.data(), &sample_count);

    GT_Runtime_Info info;
    core_->GetRuntimeInfo(info);
    // Geartowns renders the frame with a row stride of `screen_width`
    // (the render surface width); `display_width` is the visible area used
    // for scaling and may differ from the buffer layout.
    frame_width_ = info.screen_width;
    frame_height_ = info.screen_height;
    if (frame_width_ <= 0 || frame_height_ <= 0) {
        frame_width_ = GT_FRAME_BUFFER_WIDTH;
        frame_height_ = GT_FRAME_BUFFER_HEIGHT;
    }

    // Geartowns reports `sample_count` as the number of interleaved s16
    // values (already stereo pairs), so per-channel is half that.
    audio_samples_per_channel_ = static_cast<size_t>(sample_count) / 2;
}

const uint32_t* FMTownsEngine::get_framebuffer(int& width, int& height) {
    if (!initialized_ || framebuffer_.empty()) {
        width = 0;
        height = 0;
        return nullptr;
    }
    width = frame_width_;
    height = frame_height_;
    return framebuffer_.data();
}

const int16_t* FMTownsEngine::get_audio_samples(size_t& sample_count) {
    if (!initialized_ || audio_.empty()) {
        sample_count = 0;
        return nullptr;
    }
    sample_count = audio_samples_per_channel_;
    return audio_.data();
}

void FMTownsEngine::send_input(uint32_t key_code, bool pressed) {
    if (!initialized_) {
        return;
    }
    // Key codes are GT_Keys values (see Geartowns common/types.h).
    const GT_Keys key = static_cast<GT_Keys>(key_code);
    if (pressed) {
        core_->KeyPressed(key);
    } else {
        core_->KeyReleased(key);
    }
}

void FMTownsEngine::reset() {
    if (!initialized_) {
        return;
    }
    core_->ResetMedia();
    frame_width_ = 0;
    frame_height_ = 0;
    audio_samples_per_channel_ = 0;
}

void FMTownsEngine::shutdown() {
    if (core_) {
        core_->PowerOff();
        core_.reset();
    }
    framebuffer_.clear();
    framebuffer_.shrink_to_fit();
    audio_.clear();
    audio_.shrink_to_fit();
    initialized_ = false;
}

void FMTownsEngine::set_machine_model(int model) {
    if (!initialized_) {
        machine_model_ = model;
    }
}

void FMTownsEngine::set_boot_from_cd(bool enabled) {
    if (!initialized_) {
        boot_from_cd_ = enabled;
    }
}

}  // namespace asuka::plugins::fmtowns
