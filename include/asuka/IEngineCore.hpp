#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace asuka {

// Unified engine plugin interface.
//
// Every emulation core (a "plugin") implements this pure-virtual interface.
// The design is a simplified, object-oriented take on the Libretro callback
// paradigm: the host owns the main loop and pulls state (framebuffer, audio)
// from the core after asking it to advance one frame.
//
// Lifetime contract:
//   init() -> [ load_media() -> step_frame()* ] -> shutdown()
// The plugin is destroyed by the host through the matching DestroyEngineFunc.
class IEngineCore {
public:
    virtual ~IEngineCore() = default;

    // One-time initialisation. `system_path` points at the directory holding
    // the core's runtime assets (ROMs, config); `bios_path` at the directory
    // holding system BIOS/firmware images. Returns false on failure.
    virtual bool init(const std::string& system_path,
                      const std::string& bios_path) = 0;

    // Load a media image (ROM cartridge, disk, tape, ...). Returns false on
    // failure. May be called again to swap media without re-init().
    virtual bool load_media(const std::string& media_path) = 0;

    // Advance emulation by exactly one video frame, producing a fresh
    // framebuffer and audio sample batch for the getters below.
    virtual void step_frame() = 0;

    // Return a pointer to the current frame as 32-bit pixels. The layout is
    // XRGB8888 (0xAARRGGBB in memory as uint32_t). `width` and `height` are
    // written with the frame dimensions. The buffer is owned by the core and
    // remains valid until the next step_frame(). Returns nullptr if no frame
    // is available.
    virtual const uint32_t* get_framebuffer(int& width, int& height) = 0;

    // Return signed 16-bit interleaved stereo PCM accumulated by the last
    // step_frame(). `sample_count` is written with the number of samples per
    // channel (i.e. the buffer holds 2 * sample_count int16_t values). The
    // buffer is owned by the core and valid until the next step_frame().
    virtual const int16_t* get_audio_samples(size_t& sample_count) = 0;

    // Feed an input event. `key_code` is a core-defined button identifier;
    // `pressed` is true on key-down and false on key-up.
    virtual void send_input(uint32_t key_code, bool pressed) = 0;

    // Serialise the complete machine state (CPU, memory, devices) to
    // `filepath` so it can be restored exactly with load_snapshot(). Cores
    // that can render also write a visual thumbnail next to the state file
    // (see the core's documentation for the sidecar naming). Returns false on
    // failure.
    virtual bool save_snapshot(const std::string& filepath) = 0;

    // Restore a state previously written by save_snapshot(). Returns false on
    // failure (missing/corrupt file, or a state for different media).
    virtual bool load_snapshot(const std::string& filepath) = 0;

    // Soft-reset the machine while keeping the media loaded.
    virtual void reset() = 0;

    // Release every resource acquired by init()/load_media(). Safe to call
    // more than once.
    virtual void shutdown() = 0;
};

}  // namespace asuka

// Factory entry points exported by every plugin shared library. The host
// resolves these symbols dynamically (dlsym / GetProcAddress).
typedef asuka::IEngineCore* (*CreateEngineFunc)();
typedef void (*DestroyEngineFunc)(asuka::IEngineCore*);

// Canonical symbol names a plugin must export.
#define ASUKA_CREATE_ENGINE_SYMBOL "create_engine"
#define ASUKA_DESTROY_ENGINE_SYMBOL "destroy_engine"
