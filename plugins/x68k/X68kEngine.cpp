#include "X68kEngine.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "asuka/Input.hpp"

// PX68k's Libretro front-end and the globals it exposes. Every one of these
// headers carries its own extern "C" guard, so they can be included directly
// from C++.
#include "libretro.h"

namespace asuka::plugins::x68k {

// The core builds its firmware directory as "<system_dir>/keropi" and then
// resolves iplrom.dat/cgrom.dat relative to it, so the adapter rewrites the
// path once the core has finished with it (see apply_bios_path()).
extern "C" char retro_system_conf[512];

namespace {

// --- Libretro frontend glue -------------------------------------------------
//
// The core is a Libretro module, so the host answers its environment queries
// and hands it video/audio/input callbacks. Only one engine instance exists per
// process, so the callback target lives in file scope.

X68kEngine* g_engine = nullptr;
std::string g_system_dir;

// The core assumes a log callback is present: several paths call log_cb()
// without a NULL check (e.g. SRAM_WriteEnable), so answering
// RETRO_ENVIRONMENT_GET_LOG_INTERFACE with a valid sink is mandatory, not
// optional. Everything is routed to stderr.
void log_cb_printf(enum retro_log_level level, const char* fmt, ...) {
    static const char* const kLevels[] = {"debug", "info", "warn", "error"};
    const char* name = (level >= 0 && level <= RETRO_LOG_ERROR) ? kLevels[level]
                                                                : "info";
    std::fprintf(stderr, "[x68k/%s] ", name);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}

// Held keys, indexed by the asuka::Key value the host feeds to send_input().
// The core polls the pad through input_state_cb during retro_run(), so the
// adapter keeps the button state here and answers from it.
uint32_t g_held_keys = 0;

// Held keyboard keys, indexed by RetroKey (SDLK_*) code. PX68k polls all 320
// keyboard positions through input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, i)
// every frame, so the adapter answers from this bitmap.
constexpr int kKeyboardKeys = 320;
bool g_held_keys_kbd[kKeyboardKeys] = {};

// The RetroPad slots the two host action keys drive. PX68k's 2-button pad
// table maps RetroPad B to JOY_TRG1 (primary fire) and RetroPad A to JOY_TRG2,
// so the host's primary key must land on RetroPad B.
constexpr int kRetroPadPrimaryFire = RETRO_DEVICE_ID_JOYPAD_B;   // -> TRG1
constexpr int kRetroPadSecondaryFire = RETRO_DEVICE_ID_JOYPAD_A; // -> TRG2
constexpr uint32_t kPadStartBit = 1u << RETRO_DEVICE_ID_JOYPAD_START;
constexpr uint32_t kPadFireBit = 1u << kRetroPadPrimaryFire;

bool environ_cb(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            // The core reads firmware from this directory (appending
            // "/keropi" itself, which apply_bios_path() then rewrites).
            *static_cast<const char**>(data) = g_system_dir.c_str();
            return true;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
            *static_cast<const char**>(data) = g_system_dir.c_str();
            return true;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            auto* log = static_cast<retro_log_callback*>(data);
            log->log = log_cb_printf;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            // The core asks for RGB565; the adapter expands it to XRGB8888.
            return true;

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false;
            return true;

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
            return true;

        default:
            // GET_VARIABLE, GET_MIDI_INTERFACE, GET_RUMBLE_INTERFACE,
            // GET_VFS_INTERFACE, GET_INPUT_BITMASKS, GET_SAVESTATE_CONTEXT,
            // ... : report "unsupported" so the core falls back to its own
            // defaults.
            return false;
    }
}

void video_cb(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (g_engine == nullptr || data == nullptr || width == 0 || height == 0) {
        return;
    }
    g_engine->capture_frame(data, width, height, pitch);
}

size_t audio_cb(const int16_t* data, size_t frames) {
    if (g_engine != nullptr && data != nullptr && frames > 0) {
        g_engine->capture_audio(data, frames);
    }
    return frames;
}

void input_poll_cb() {}

int16_t input_state_cb(unsigned port, unsigned device, unsigned /*index*/,
                       unsigned id) {
    if (port != 0) {
        return 0;
    }
    if (device == RETRO_DEVICE_JOYPAD) {
        return (g_held_keys & (1u << id)) != 0 ? 1 : 0;
    }
    if (device == RETRO_DEVICE_KEYBOARD) {
        if (id < static_cast<unsigned>(kKeyboardKeys) && g_held_keys_kbd[id]) {
            return 1;
        }
        // Dual-dispatch: X68000 games read "start" and "fire" from the keyboard
        // (the 2-button pad exposes neither), so the host action keys also
        // answer for RETURN and SPACE here. Reading the pad bitmap keeps the
        // two paths in sync without a second piece of held-state to maintain.
        if (id == static_cast<unsigned>(RETROK_RETURN) &&
            (g_held_keys & kPadStartBit) != 0) {
            return 1;
        }
        if (id == static_cast<unsigned>(RETROK_SPACE) &&
            (g_held_keys & kPadFireBit) != 0) {
            return 1;
        }
        return 0;
    }
    return 0;
}

// Translate a host key to the matching Libretro joypad button id, which the
// core maps onto X68000 Joypad 1 (see joystick.c's retro pad table). The host's
// primary fire key lands on the RetroPad slot PX68k turns into JOY_TRG1.
int ToJoypadId(Key key) {
    switch (key) {
        case Key::Up:     return RETRO_DEVICE_ID_JOYPAD_UP;
        case Key::Down:   return RETRO_DEVICE_ID_JOYPAD_DOWN;
        case Key::Left:   return RETRO_DEVICE_ID_JOYPAD_LEFT;
        case Key::Right:  return RETRO_DEVICE_ID_JOYPAD_RIGHT;
        case Key::Start:  return RETRO_DEVICE_ID_JOYPAD_START;
        case Key::Select: return RETRO_DEVICE_ID_JOYPAD_SELECT;
        case Key::A:      return kRetroPadPrimaryFire;
        case Key::B:      return kRetroPadSecondaryFire;
        case Key::X:      return RETRO_DEVICE_ID_JOYPAD_X;
        case Key::Y:      return RETRO_DEVICE_ID_JOYPAD_Y;
        case Key::L:      return RETRO_DEVICE_ID_JOYPAD_L;
        case Key::R:      return RETRO_DEVICE_ID_JOYPAD_R;
        case Key::None:   break;
    }
    return -1;
}

// Sidecar thumbnail path for a state file: swap the extension for ".bmp"
// ("slot1.state" -> "slot1.bmp", "slot1" -> "slot1.bmp").
std::string ThumbnailPath(const std::string& state_path) {
    std::string path = state_path;
    const std::string::size_type dot = path.find_last_of('.');
    const std::string::size_type slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        path.erase(dot);
    }
    path += ".bmp";
    return path;
}

}  // namespace

X68kEngine::X68kEngine() = default;

X68kEngine::~X68kEngine() { shutdown(); }

bool X68kEngine::init(const std::string& system_path,
                      const std::string& bios_path) {
    if (initialized_) {
        return true;
    }

    system_path_ = system_path;
    bios_path_ = bios_path;

    // The core resolves firmware relative to the system directory it is given
    // through the environment callback; a directory is always required.
    g_system_dir = bios_path.empty() ? "." : bios_path;
    g_engine = this;
    g_held_keys = 0;

    retro_set_environment(environ_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample_batch(audio_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    // Enable the emulated 2-button X68000 Joypad 1 so the pad path is live.
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

    initialized_ = true;

    if (bios_path_.empty()) {
        std::fprintf(stderr,
                     "asuka/x68k: no BIOS directory supplied; PX68k needs "
                     "iplrom.dat and cgrom.dat to boot\n");
    }
    return true;
}

void X68kEngine::apply_bios_path() {
    // The core builds its firmware directory as "<system_dir>/keropi";
    // redirect it to the directory the host actually supplied.
    if (bios_path_.empty()) {
        return;
    }
    std::snprintf(retro_system_conf, sizeof(retro_system_conf), "%s",
                  bios_path_.c_str());
}

bool X68kEngine::load_media(const std::string& media_path) {
    if (!initialized_) {
        return false;
    }

    retro_game_info info{};
    info.path = media_path.c_str();
    if (!retro_load_game(media_path.empty() ? nullptr : &info)) {
        std::fprintf(stderr, "asuka/x68k: cannot load media '%s'\n",
                     media_path.c_str());
        return false;
    }
    media_path_ = media_path;
    game_loaded_ = true;
    apply_bios_path();

    // Media is mounted by the core's one-off bootstrap, so re-arm it.
    primed_ = false;
    return true;
}

void X68kEngine::prime() {
    // The core needs a game (possibly an empty one) registered before its
    // first retro_run() performs the one-off bootstrap (pmain) that powers the
    // machine on and mounts media.
    if (!game_loaded_) {
        retro_load_game(nullptr);
        game_loaded_ = true;
        apply_bios_path();
    }

    retro_run();  // bootstrap pass: produces no frame
    primed_ = true;
}

void X68kEngine::step_frame() {
    if (!initialized_) {
        return;
    }

    audio_.clear();
    audio_samples_per_channel_ = 0;

    if (!primed_) {
        prime();
    }

    retro_run();
}

void X68kEngine::capture_frame(const void* data, unsigned width,
                               unsigned height, size_t pitch) {
    frame_width_ = static_cast<int>(width);
    frame_height_ = static_cast<int>(height);
    framebuffer_.assign(static_cast<size_t>(width) * height, 0xFF000000u);

    // PX68k emits RGB565 (16-bit). Expand every pixel to XRGB8888.
    const auto* src = static_cast<const uint8_t*>(data);
    for (unsigned y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const uint16_t*>(src + y * pitch);
        uint32_t* dst = framebuffer_.data() + static_cast<size_t>(y) * width;
        for (unsigned x = 0; x < width; ++x) {
            const uint16_t px = row[x];
            const uint32_t r = static_cast<uint32_t>((px >> 11) & 0x1f);
            const uint32_t g = static_cast<uint32_t>((px >> 5) & 0x3f);
            const uint32_t b = static_cast<uint32_t>(px & 0x1f);
            dst[x] = 0xFF000000u | (((r << 3) | (r >> 2)) << 16) |
                     (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
        }
    }
}

void X68kEngine::capture_audio(const int16_t* data, size_t frames) {
    const size_t offset = audio_.size();
    audio_.resize(offset + frames * 2);
    std::memcpy(audio_.data() + offset, data, frames * 2 * sizeof(int16_t));
    audio_samples_per_channel_ = audio_.size() / 2;
}

const uint32_t* X68kEngine::get_framebuffer(int& width, int& height) {
    if (!initialized_ || framebuffer_.empty()) {
        width = 0;
        height = 0;
        return nullptr;
    }
    width = frame_width_;
    height = frame_height_;
    return framebuffer_.data();
}

const int16_t* X68kEngine::get_audio_samples(size_t& sample_count) {
    if (!initialized_ || audio_.empty()) {
        sample_count = 0;
        return nullptr;
    }
    sample_count = audio_samples_per_channel_;
    return audio_.data();
}

void X68kEngine::send_input(uint32_t key_code, bool pressed) {
    if (!initialized_) {
        return;
    }
    const int id = ToJoypadId(static_cast<Key>(key_code));
    if (id < 0) {
        return;
    }
    const uint32_t bit = 1u << static_cast<unsigned>(id);
    if (pressed) {
        g_held_keys |= bit;
    } else {
        g_held_keys &= ~bit;
    }
}

void X68kEngine::send_scancode(int scancode, bool pressed) {
    if (!initialized_) {
        return;
    }
    if (scancode < 0 || scancode >= kKeyboardKeys) {
        return;
    }
    g_held_keys_kbd[scancode] = pressed;
}

bool X68kEngine::save_snapshot(const std::string& filepath) {
    if (!initialized_ || filepath.empty()) {
        return false;
    }
    if (!primed_) {
        prime();
    }
    const size_t size = retro_serialize_size();
    if (size == 0) {
        std::fprintf(stderr, "asuka/x68k: cannot size snapshot\n");
        return false;
    }
    std::vector<uint8_t> buffer(size);
    if (!retro_serialize(buffer.data(), buffer.size())) {
        std::fprintf(stderr, "asuka/x68k: cannot serialize snapshot\n");
        return false;
    }
    std::ofstream out(filepath, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "asuka/x68k: cannot open snapshot '%s'\n",
                     filepath.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    if (!out.good()) {
        return false;
    }
    out.close();

    // A missing thumbnail is not fatal: the state is what matters.
    write_thumbnail_bmp(filepath);
    return true;
}

bool X68kEngine::load_snapshot(const std::string& filepath) {
    if (!initialized_ || filepath.empty()) {
        return false;
    }
    // The core must have booted once before its state can be restored, so a
    // load issued straight after init (as the runner does) primes first.
    if (!primed_) {
        prime();
    }
    std::ifstream in(filepath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "asuka/x68k: cannot open snapshot '%s'\n",
                     filepath.c_str());
        return false;
    }
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    if (buffer.empty()) {
        return false;
    }
    if (!retro_unserialize(buffer.data(), buffer.size())) {
        std::fprintf(stderr, "asuka/x68k: cannot restore snapshot '%s'\n",
                     filepath.c_str());
        return false;
    }
    return true;
}

void X68kEngine::reset() {
    if (!initialized_ || !primed_) {
        return;
    }
    retro_reset();
    framebuffer_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
    audio_.clear();
    audio_samples_per_channel_ = 0;
}

void X68kEngine::shutdown() {
    if (initialized_) {
        retro_unload_game();
        retro_deinit();
    }
    g_engine = nullptr;
    g_held_keys = 0;
    std::memset(g_held_keys_kbd, 0, sizeof(g_held_keys_kbd));
    framebuffer_.clear();
    framebuffer_.shrink_to_fit();
    audio_.clear();
    audio_.shrink_to_fit();
    frame_width_ = 0;
    frame_height_ = 0;
    audio_samples_per_channel_ = 0;
    primed_ = false;
    game_loaded_ = false;
    initialized_ = false;
}

// Write the most recent frame as a bottom-up 24-bit BMP (BGR rows, padded to
// 4 bytes). No scaling: the web shell downscales for the preview itself.
bool X68kEngine::write_thumbnail_bmp(const std::string& state_path) const {
    if (frame_width_ <= 0 || frame_height_ <= 0 || framebuffer_.empty()) {
        return false;
    }

    const int width = frame_width_;
    const int height = frame_height_;
    const int row_bytes = width * 3;
    const int padding = (4 - (row_bytes % 4)) % 4;
    const uint32_t pixel_bytes =
        static_cast<uint32_t>((row_bytes + padding) * height);
    const uint32_t file_bytes = 14 + 40 + pixel_bytes;

    std::ofstream out(ThumbnailPath(state_path), std::ios::binary);
    if (!out) {
        return false;
    }

    const auto u16 = [&out](uint16_t v) {
        const uint8_t b[2] = {static_cast<uint8_t>(v & 0xff),
                              static_cast<uint8_t>((v >> 8) & 0xff)};
        out.write(reinterpret_cast<const char*>(b), 2);
    };
    const auto u32 = [&out](uint32_t v) {
        const uint8_t b[4] = {static_cast<uint8_t>(v & 0xff),
                              static_cast<uint8_t>((v >> 8) & 0xff),
                              static_cast<uint8_t>((v >> 16) & 0xff),
                              static_cast<uint8_t>((v >> 24) & 0xff)};
        out.write(reinterpret_cast<const char*>(b), 4);
    };

    // BITMAPFILEHEADER
    out.put('B');
    out.put('M');
    u32(file_bytes);
    u16(0);
    u16(0);
    u32(14 + 40);
    // BITMAPINFOHEADER
    u32(40);
    u32(static_cast<uint32_t>(width));
    u32(static_cast<uint32_t>(height));  // positive: bottom-up rows
    u16(1);
    u16(24);
    u32(0);
    u32(pixel_bytes);
    u32(2835);
    u32(2835);
    u32(0);
    u32(0);

    // BMP rows run bottom-up; the framebuffer is top-down XRGB8888.
    const uint8_t pad[3] = {0, 0, 0};
    const size_t stride = static_cast<size_t>(width);
    for (int y = height - 1; y >= 0; --y) {
        const uint32_t* row =
            framebuffer_.data() + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            const uint32_t px = row[x];
            const uint8_t bgr[3] = {static_cast<uint8_t>(px & 0xff),
                                    static_cast<uint8_t>((px >> 8) & 0xff),
                                    static_cast<uint8_t>((px >> 16) & 0xff)};
            out.write(reinterpret_cast<const char*>(bgr), 3);
        }
        if (padding > 0) {
            out.write(reinterpret_cast<const char*>(pad), padding);
        }
    }

    return out.good();
}

}  // namespace asuka::plugins::x68k
