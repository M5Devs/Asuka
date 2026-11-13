#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "asuka/IEngineCore.hpp"

namespace asuka::plugins::dummy {

// A mock engine core used to prove the plugin ABI and dynamic loading work.
//
// It performs no emulation. Instead step_frame() synthesises a moving colour
// pattern into an XRGB8888 framebuffer and a stereo sine tone into a signed
// 16-bit PCM buffer, so the host can visually and audibly verify that frames
// are being produced.
class DummyEngine final : public IEngineCore {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 240;
    static constexpr int kAudioRate = 48000;

    DummyEngine() = default;
    ~DummyEngine() override = default;

    bool init(const std::string& system_path,
              const std::string& bios_path) override;
    bool load_media(const std::string& media_path) override;
    void step_frame() override;
    const uint32_t* get_framebuffer(int& width, int& height) override;
    const int16_t* get_audio_samples(size_t& sample_count) override;
    void send_input(uint32_t key_code, bool pressed) override;
    void reset() override;
    void shutdown() override;

private:
    void render_frame();

    bool initialized_ = false;
    bool media_loaded_ = false;
    uint64_t frame_counter_ = 0;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;

    // Audio synthesis phase (per channel), advanced each frame.
    double tone_phase_ = 0.0;
    // Most recent input event, kept only to demonstrate the callback path.
    uint32_t last_key_ = 0;
    bool last_key_pressed_ = false;
};

}  // namespace asuka::plugins::dummy
