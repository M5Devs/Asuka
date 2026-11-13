#include "FMTownsEngine.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "asuka/Input.hpp"
#include "geartowns.h"

namespace asuka::plugins::fmtowns {

namespace {

// Geartowns' maximum render surface; the actual frame is usually smaller.
constexpr int kMaxWidth = GT_MAX_FRAME_BUFFER_WIDTH;    // 1024
constexpr int kMaxHeight = GT_MAX_FRAME_BUFFER_HEIGHT;  // 768

// Geartowns accumulates one frame of interleaved stereo PCM into a buffer
// capped at GT_AUDIO_BUFFER_SIZE values and then copies the whole frame out,
// so the destination must be at least that large (a per-frame estimate is too
// small and overflows whenever the audio clock runs ahead of the video clock).
constexpr size_t kMaxAudioValues = GT_AUDIO_BUFFER_SIZE;

// Translate a host key to the FM Towns keyboard key it stands for. Movement
// uses the cursor keys; the action buttons land on the letters FM Towns games
// most often bind to fire.
GT_Keys ToGtKey(Key key) {
    switch (key) {
        case Key::Up:     return GT_KEY_UP;
        case Key::Down:   return GT_KEY_DOWN;
        case Key::Left:   return GT_KEY_LEFT;
        case Key::Right:  return GT_KEY_RIGHT;
        case Key::Start:  return GT_KEY_RETURN;
        case Key::Select: return GT_KEY_SPACE;
        case Key::A:      return GT_KEY_SPACE;
        case Key::B:      return GT_KEY_Z;
        case Key::X:      return GT_KEY_X;
        case Key::Y:      return GT_KEY_C;
        case Key::L:      return GT_KEY_Z;
        case Key::R:      return GT_KEY_X;
        case Key::None:   break;
    }
    return GT_KEY_NONE;
}

// Translate a host key to a Game Port 1 (Towns Pad) button. FM Towns arcade
// titles poll the pad rather than the keyboard, so every input is mirrored
// there in addition to the keyboard. A/B map onto the original pad; X/Y/L/R
// use the extra buttons the six-button pad exposes.
u16 ToGamePadButton(Key key) {
    switch (key) {
        case Key::Up:     return GT_GAMEPAD_UP;
        case Key::Down:   return GT_GAMEPAD_DOWN;
        case Key::Left:   return GT_GAMEPAD_LEFT;
        case Key::Right:  return GT_GAMEPAD_RIGHT;
        case Key::Start:  return GT_GAMEPAD_RUN;
        case Key::Select: return GT_GAMEPAD_SELECT;
        case Key::A:      return GT_GAMEPAD_A;
        case Key::B:      return GT_GAMEPAD_B;
        case Key::X:      return GT_GAMEPAD_X;
        case Key::Y:      return GT_GAMEPAD_Y;
        case Key::L:      return GT_GAMEPAD_Z;
        case Key::R:      return GT_GAMEPAD_C;
        case Key::None:   break;
    }
    return 0;
}

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
    // `key_code` is an asuka::Key value. Every input goes to the FM Towns
    // keyboard *and* to Game Port 1 (Towns Pad): arcade titles poll the game
    // port rather than the keyboard, so without the pad mirror the title
    // screen of a game like After Burner ignores the controls. The keyboard
    // mirror keeps the mapping useful for software that reads the keyboard.
    const Key key = static_cast<Key>(key_code);
    const GT_Keys gt_key = ToGtKey(key);
    if (gt_key != GT_KEY_NONE) {
        if (pressed) {
            core_->KeyPressed(gt_key);
        } else {
            core_->KeyReleased(gt_key);
        }
    }

    const u16 button = ToGamePadButton(key);
    if (button == 0) {
        return;
    }
    if (pressed) {
        gamepad_buttons_ |= button;
    } else {
        gamepad_buttons_ &= static_cast<u16>(~button);
    }
    GT_GamePad_State state = {};
    state.buttons = gamepad_buttons_;
    core_->GetInput()->SetGamePadState(GT_CONTROLLER_1, state);
}

void FMTownsEngine::reset() {
    if (!initialized_) {
        return;
    }
    core_->ResetMedia();
    frame_width_ = 0;
    frame_height_ = 0;
    audio_samples_per_channel_ = 0;

    // Drop any held pad buttons so a reset does not leave Game Port 1 stuck.
    gamepad_buttons_ = 0;
    GT_GamePad_State state = {};
    core_->GetInput()->SetGamePadState(GT_CONTROLLER_1, state);
}

void FMTownsEngine::shutdown() {
    if (core_) {
        core_->PowerOff();
        core_.reset();
    }
    gamepad_buttons_ = 0;
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
