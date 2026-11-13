#include "DummyEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>

namespace asuka::plugins::dummy {

namespace {
constexpr double kPi = 3.14159265358979323846;
// Nominal 60 Hz frame rate -> samples of audio to synthesise per frame.
constexpr int kFrameRate = 60;
constexpr int kSamplesPerFrame = DummyEngine::kAudioRate / kFrameRate;  // 800
constexpr double kToneHz = 440.0;
}  // namespace

bool DummyEngine::init(const std::string& system_path,
                       const std::string& bios_path) {
    (void)system_path;  // The dummy core ignores all paths.
    (void)bios_path;

    if (initialized_) {
        return true;
    }

    framebuffer_.assign(static_cast<size_t>(kWidth) * kHeight, 0xFF000000u);
    audio_.assign(static_cast<size_t>(kSamplesPerFrame) * 2, 0);
    frame_counter_ = 0;
    tone_phase_ = 0.0;
    initialized_ = true;
    return true;
}

bool DummyEngine::load_media(const std::string& media_path) {
    if (!initialized_) {
        return false;
    }
    // Any path is accepted; an empty path is treated as "no media".
    media_loaded_ = !media_path.empty();
    return true;
}

void DummyEngine::step_frame() {
    if (!initialized_) {
        return;
    }
    render_frame();
    ++frame_counter_;
}

void DummyEngine::render_frame() {
    // Colour cycle: hue sweeps with the frame counter. Each scanline is
    // offset slightly so the animation is obviously non-static.
    const double hue_phase =
        static_cast<double>(frame_counter_ % 360) * (kPi / 180.0);

    for (int y = 0; y < kHeight; ++y) {
        const double row_shift =
            static_cast<double>(y) / static_cast<double>(kHeight) * 2.0 * kPi;
        const double angle = hue_phase + row_shift;
        // Simple RGB triple in [0,255]; the alpha channel stays 0xFF.
        const auto r = static_cast<uint32_t>(
            (std::sin(angle) * 0.5 + 0.5) * 255.0);
        const auto g = static_cast<uint32_t>(
            (std::sin(angle + 2.0 * kPi / 3.0) * 0.5 + 0.5) * 255.0);
        const auto b = static_cast<uint32_t>(
            (std::sin(angle + 4.0 * kPi / 3.0) * 0.5 + 0.5) * 255.0);
        const uint32_t pixel = 0xFF000000u | (r << 16) | (g << 8) | b;

        uint32_t* row =
            framebuffer_.data() + static_cast<size_t>(y) * kWidth;
        for (int x = 0; x < kWidth; ++x) {
            row[x] = pixel;
        }
    }

    // Stereo sine tone; advance the phase across the frame's samples.
    const double step = 2.0 * kPi * kToneHz /
                        static_cast<double>(kAudioRate);
    for (int i = 0; i < kSamplesPerFrame; ++i) {
        const auto sample =
            static_cast<int16_t>(std::sin(tone_phase_) * 8000.0);
        audio_[static_cast<size_t>(i) * 2] = sample;      // left
        audio_[static_cast<size_t>(i) * 2 + 1] = sample;  // right
        tone_phase_ += step;
        if (tone_phase_ > 2.0 * kPi) {
            tone_phase_ -= 2.0 * kPi;
        }
    }
}

const uint32_t* DummyEngine::get_framebuffer(int& width, int& height) {
    if (!initialized_ || framebuffer_.empty()) {
        width = 0;
        height = 0;
        return nullptr;
    }
    width = kWidth;
    height = kHeight;
    return framebuffer_.data();
}

const int16_t* DummyEngine::get_audio_samples(size_t& sample_count) {
    if (!initialized_ || audio_.empty()) {
        sample_count = 0;
        return nullptr;
    }
    sample_count = audio_.size() / 2;
    return audio_.data();
}

void DummyEngine::send_input(uint32_t key_code, bool pressed) {
    last_key_ = key_code;
    last_key_pressed_ = pressed;
}

// The dummy has no machine to serialise, so a snapshot is just the frame
// counter, written as a small binary blob. It exists so the snapshot ABI is
// exercised end to end (host -> plugin -> filesystem) without an emulator.
bool DummyEngine::save_snapshot(const std::string& filepath) {
    if (!initialized_ || filepath.empty()) {
        return false;
    }
    std::ofstream out(filepath, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(&frame_counter_), sizeof(frame_counter_));
    return out.good();
}

bool DummyEngine::load_snapshot(const std::string& filepath) {
    if (!initialized_ || filepath.empty()) {
        return false;
    }
    std::ifstream in(filepath, std::ios::binary);
    if (!in) {
        return false;
    }
    uint64_t counter = 0;
    in.read(reinterpret_cast<char*>(&counter), sizeof(counter));
    if (!in) {
        return false;
    }
    frame_counter_ = counter;
    return true;
}

void DummyEngine::reset() {
    frame_counter_ = 0;
    tone_phase_ = 0.0;
    if (initialized_) {
        std::fill(audio_.begin(), audio_.end(), static_cast<int16_t>(0));
    }
}

void DummyEngine::shutdown() {
    framebuffer_.clear();
    framebuffer_.shrink_to_fit();
    audio_.clear();
    audio_.shrink_to_fit();
    initialized_ = false;
    media_loaded_ = false;
    frame_counter_ = 0;
    tone_phase_ = 0.0;
}

}  // namespace asuka::plugins::dummy
