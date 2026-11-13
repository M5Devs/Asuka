#include "PC98Engine.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "asuka/Input.hpp"

// NP2kai's Libretro front-end and the core headers it exposes. compiler.h must
// come first: it defines the integer typedefs (UINT8, REG8, BOOL, ...) the rest
// of the core headers rely on. Every one of these headers carries its own
// extern "C" guard, so they can be included directly from C++.
#include "compiler.h"
#include "pccore.h"
#include "keystat.h"
#include "statsave.h"
#include "dosio.h"
#include "libretro.h"
#include "np2.h"

namespace asuka::plugins::pc98 {

namespace {

// --- Libretro frontend glue -------------------------------------------------
//
// The core is a Libretro module, so the host answers its environment queries
// and hands it video/audio/input callbacks. Only one engine instance exists per
// process, so the callback target lives in file scope.

PC98Engine* g_engine = nullptr;
std::string g_system_dir;

bool environ_cb(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            // The core reads firmware from this directory. It appends
            // "/np2kai" itself, so the adapter overrides the final path once
            // the core has finished with it (see apply_bios_path()).
            *static_cast<const char**>(data) = g_system_dir.c_str();
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return true;

        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            // Force the CL-GD5430 (WAB) path on. It is what makes the core
            // render 32-bit XRGB8888, the layout IEngineCore expects; without
            // it the core emits RGB565. Every other option keeps the core's
            // built-in default.
            auto* var = static_cast<retro_variable*>(data);
            if (var != nullptr && var->key != nullptr &&
                std::strcmp(var->key, "np2kai_CLGD_en") == 0) {
                var->value = "ON";
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false;
            return true;

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        case RETRO_ENVIRONMENT_SET_VARIABLES:
            return true;

        default:
            // GET_LOG_INTERFACE, GET_MIDI_INTERFACE, GET_CORE_OPTIONS_VERSION,
            // GET_LANGUAGE and anything else: report "unsupported" so the core
            // falls back to its own defaults.
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

int16_t input_state_cb(unsigned /*port*/, unsigned /*device*/,
                       unsigned /*index*/, unsigned /*id*/) {
    // All input is injected straight into the keyboard matrix (see
    // send_input/send_scancode), so the polled-input path stays idle.
    return 0;
}

// Translate a host key to a PC-98 keyboard make code (0x00-based). Directions
// use the cursor keys; the action buttons land on the keys PC-98 games most
// often bind, following WebNP2's default "Cursor + Z/X" preset
// (A->Z, B->X, Start->RETURN, Select->ESC).
int ToPc98Scancode(Key key) {
    switch (key) {
        case Key::Up:     return 0x3a;
        case Key::Down:   return 0x3d;
        case Key::Left:   return 0x3b;
        case Key::Right:  return 0x3c;
        case Key::Start:  return 0x1c;  // RETURN
        case Key::Select: return 0x00;  // ESC
        case Key::A:      return 0x29;  // Z
        case Key::B:      return 0x2a;  // X
        case Key::X:      return 0x34;  // SPACE
        case Key::Y:      return 0x1c;  // RETURN
        case Key::L:      return 0x00;  // ESC
        case Key::R:      return 0x3f;  // HELP
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

PC98Engine::PC98Engine() = default;

PC98Engine::~PC98Engine() { shutdown(); }

bool PC98Engine::init(const std::string& system_path,
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

    retro_set_environment(environ_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample_batch(audio_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    initialized_ = true;

    if (bios_path_.empty()) {
        std::fprintf(stderr,
                     "asuka/pc98: no BIOS directory supplied; relying on the "
                     "built-in BIOS I/O emulation (bios.rom/font.bmp absent)\n");
    }
    return true;
}

void PC98Engine::apply_bios_path() {
    // retro_load_game() points the core at "<system_dir>/np2kai"; redirect it
    // to the directory the host actually supplied.
    if (bios_path_.empty()) {
        return;
    }
    std::snprintf(np2cfg.biospath, MAX_PATH, "%s/", bios_path_.c_str());
    std::snprintf(np2cfg.fontfile, MAX_PATH, "%s/font.bmp", bios_path_.c_str());
    file_setcd(np2cfg.biospath);
}

bool PC98Engine::load_media(const std::string& media_path) {
    if (!initialized_) {
        return false;
    }

    retro_game_info info{};
    info.path = media_path.c_str();
    if (!retro_load_game(media_path.empty() ? nullptr : &info)) {
        std::fprintf(stderr, "asuka/pc98: cannot load media '%s'\n",
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

void PC98Engine::prime() {
    // The core needs a game (possibly an empty one) registered before its
    // first retro_run() performs the one-off bootstrap (np2_main) that powers
    // the machine on and mounts media.
    if (!game_loaded_) {
        retro_load_game(nullptr);
        game_loaded_ = true;
        apply_bios_path();
    }

    retro_run();  // bootstrap pass: produces no frame
    primed_ = true;
}

void PC98Engine::step_frame() {
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

void PC98Engine::capture_frame(const void* data, unsigned width,
                               unsigned height, size_t pitch) {
    frame_width_ = static_cast<int>(width);
    frame_height_ = static_cast<int>(height);
    framebuffer_.assign(static_cast<size_t>(width) * height, 0xFF000000u);

    const auto* src = static_cast<const uint8_t*>(data);
    const size_t bytes_per_pixel = width != 0 ? pitch / width : 4;

    if (bytes_per_pixel >= 4) {
        for (unsigned y = 0; y < height; ++y) {
            const auto* row =
                reinterpret_cast<const uint32_t*>(src + y * pitch);
            uint32_t* dst =
                framebuffer_.data() + static_cast<size_t>(y) * width;
            for (unsigned x = 0; x < width; ++x) {
                dst[x] = 0xFF000000u | (row[x] & 0x00FFFFFFu);
            }
        }
    } else {
        // RGB565 fallback: expand to XRGB8888.
        for (unsigned y = 0; y < height; ++y) {
            const auto* row =
                reinterpret_cast<const uint16_t*>(src + y * pitch);
            uint32_t* dst =
                framebuffer_.data() + static_cast<size_t>(y) * width;
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
}

void PC98Engine::capture_audio(const int16_t* data, size_t frames) {
    const size_t offset = audio_.size();
    audio_.resize(offset + frames * 2);
    std::memcpy(audio_.data() + offset, data, frames * 2 * sizeof(int16_t));
    audio_samples_per_channel_ = audio_.size() / 2;
}

const uint32_t* PC98Engine::get_framebuffer(int& width, int& height) {
    if (!initialized_ || framebuffer_.empty()) {
        width = 0;
        height = 0;
        return nullptr;
    }
    width = frame_width_;
    height = frame_height_;
    return framebuffer_.data();
}

const int16_t* PC98Engine::get_audio_samples(size_t& sample_count) {
    if (!initialized_ || audio_.empty()) {
        sample_count = 0;
        return nullptr;
    }
    sample_count = audio_samples_per_channel_;
    return audio_.data();
}

void PC98Engine::send_scancode(int scancode, bool pressed) {
    if (!initialized_ || scancode < 0 || scancode > 0x7f) {
        return;
    }
    // The same injection point WebNP2's webnp2_key() uses: bit 7 marks a
    // release, the low 7 bits carry the PC-98 make code.
    keystat_senddata(
        static_cast<REG8>((scancode & 0x7f) | (pressed ? 0x00 : 0x80)));
}

void PC98Engine::send_input(uint32_t key_code, bool pressed) {
    if (!initialized_) {
        return;
    }
    const int scancode = ToPc98Scancode(static_cast<Key>(key_code));
    if (scancode < 0) {
        return;
    }
    send_scancode(scancode, pressed);
}

bool PC98Engine::save_snapshot(const std::string& filepath) {
    if (!initialized_ || filepath.empty()) {
        return false;
    }
    if (!primed_) {
        prime();
    }
    // statsave_save_d() ORs informational bits (STATFLAG_WARNING/VERCHG)
    // into its result, so only STATFLAG_FAILURE means the write failed.
    if (statsave_save_d(filepath.c_str()) == STATFLAG_FAILURE) {
        std::fprintf(stderr, "asuka/pc98: cannot save snapshot '%s'\n",
                     filepath.c_str());
        return false;
    }
    // A missing thumbnail is not fatal: the state is what matters.
    write_thumbnail_bmp(filepath);
    return true;
}

bool PC98Engine::load_snapshot(const std::string& filepath) {
    if (!initialized_ || filepath.empty()) {
        return false;
    }
    // The core must have booted once before its state can be restored, so a
    // load issued straight after init (as the runner does) primes first.
    if (!primed_) {
        prime();
    }
    // As with save: only STATFLAG_FAILURE indicates the restore failed; the
    // remaining bits report a version/warning note, not an error.
    if (statsave_load_d(filepath.c_str()) == STATFLAG_FAILURE) {
        std::fprintf(stderr, "asuka/pc98: cannot load snapshot '%s'\n",
                     filepath.c_str());
        return false;
    }
    return true;
}

void PC98Engine::reset() {
    if (!initialized_ || !primed_) {
        return;
    }
    pccore_reset();
    framebuffer_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
    audio_.clear();
    audio_samples_per_channel_ = 0;
}

void PC98Engine::shutdown() {
    if (initialized_ && primed_) {
        // np2_end() tears down the machine the bootstrap created.
        retro_deinit();
    }
    g_engine = nullptr;
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
bool PC98Engine::write_thumbnail_bmp(const std::string& state_path) const {
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

}  // namespace asuka::plugins::pc98
