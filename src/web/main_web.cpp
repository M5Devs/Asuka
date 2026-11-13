// Asuka web runner (Emscripten + SDL2).
//
// The browser counterpart to asuka_play: it drives the FM Towns engine on the
// page's <canvas> with Web Audio, and exposes a small C API the JavaScript
// shell calls (asuka_web_init/start/key/...). The engine is linked in
// statically (see CMakeLists.txt) rather than dlopen-ed, because dynamic
// loading across WASM modules is brittle.
//
// The host owns the loop, as the plugin ABI intends. Emscripten drives it
// through the main-loop callback pattern (one emulated frame per
// requestAnimationFrame), which is both simpler and faster than blocking on
// ASYNCIFY.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "asuka/IEngineCore.hpp"
#include "asuka/Input.hpp"

#include "fmtowns_plugin.hpp"
#if defined(ASUKA_WEB_HAS_PC98)
#include "pc98_plugin.hpp"
#endif

#include <SDL.h>
#include <emscripten.h>

namespace {

// Engine audio format (see IEngineCore): interleaved stereo s16 at 44100 Hz.
constexpr int kAudioSampleRate = 44100;
constexpr int kAudioChannels = 2;

// Snapshot slots live under /bios so the IDBFS mount persists them across
// browser reloads (the shell syncs the filesystem after each save).
constexpr const char* kSnapshotDir = "/bios/snapshots";

// How much audio to keep queued ahead of playback. Enough to ride out frame
// jitter, small enough that video and sound stay together.
constexpr int kAudioTargetMs = 80;

// Left-stick deflection beyond which a direction registers.
constexpr Sint16 kStickDeadzone = 8000;

// The four directional inputs as a bitmask, so the analog stick and the d-pad
// can be combined and edge-detected together.
enum Direction : unsigned {
    kDirUp = 1u << 0,
    kDirDown = 1u << 1,
    kDirLeft = 1u << 2,
    kDirRight = 1u << 3,
};

struct WebState {
    asuka::IEngineCore* engine = nullptr;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioDeviceID audio_device = 0;
    SDL_GameController* pad = nullptr;

    bool running = false;
    bool audio_enabled = true;
    long long frames = 0;
    unsigned long long input_events = 0;
    unsigned prev_dirs = 0;
    // Which engine the shell asked for ("fmtowns" or "pc98"); kept so the
    // matching destroy function is called on shutdown.
    std::string machine;
    // Firmware directory the shell supplied at init, reused when it swaps in a
    // game image at runtime (asuka_web_load_rom).
    std::string bios_dir;
    std::string last_error;

    // Scratch buffer holding the BMP thumbnail most recently requested by the
    // shell (asuka_web_get_snapshot_thumbnail). Kept alive until the next call
    // so the returned pointer stays valid while JavaScript reads it.
    std::vector<uint8_t> thumbnail;
};

WebState g_state;

void set_error(const std::string& message) {
    g_state.last_error = message;
    std::fprintf(stderr, "asuka/web: %s\n", message.c_str());
}

// Map a game controller button to a host key.
asuka::Key key_from_button(Uint8 button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return asuka::Key::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return asuka::Key::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return asuka::Key::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return asuka::Key::Right;
        case SDL_CONTROLLER_BUTTON_A:          return asuka::Key::A;
        case SDL_CONTROLLER_BUTTON_B:          return asuka::Key::B;
        case SDL_CONTROLLER_BUTTON_X:          return asuka::Key::X;
        case SDL_CONTROLLER_BUTTON_Y:          return asuka::Key::Y;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return asuka::Key::L;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return asuka::Key::R;
        case SDL_CONTROLLER_BUTTON_START:      return asuka::Key::Start;
        case SDL_CONTROLLER_BUTTON_BACK:       return asuka::Key::Select;
        default:                               return asuka::Key::None;
    }
}

unsigned directions_from_pad(SDL_GameController* pad) {
    unsigned dirs = 0;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0 ||
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) < -kStickDeadzone) {
        dirs |= kDirUp;
    }
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0 ||
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) > kStickDeadzone) {
        dirs |= kDirDown;
    }
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0 ||
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) < -kStickDeadzone) {
        dirs |= kDirLeft;
    }
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0 ||
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) > kStickDeadzone) {
        dirs |= kDirRight;
    }
    return dirs;
}

void send_dirs(unsigned dirs) {
    const unsigned changed = dirs ^ g_state.prev_dirs;
    if ((changed & kDirUp) != 0) {
        g_state.engine->send_input(static_cast<uint32_t>(asuka::Key::Up),
                                   (dirs & kDirUp) != 0);
    }
    if ((changed & kDirDown) != 0) {
        g_state.engine->send_input(static_cast<uint32_t>(asuka::Key::Down),
                                   (dirs & kDirDown) != 0);
    }
    if ((changed & kDirLeft) != 0) {
        g_state.engine->send_input(static_cast<uint32_t>(asuka::Key::Left),
                                   (dirs & kDirLeft) != 0);
    }
    if ((changed & kDirRight) != 0) {
        g_state.engine->send_input(static_cast<uint32_t>(asuka::Key::Right),
                                   (dirs & kDirRight) != 0);
    }
    g_state.prev_dirs = dirs;
}

void handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            const bool pressed = event.type == SDL_KEYDOWN;
            asuka::Key key = asuka::Key::None;
            switch (event.key.keysym.scancode) {
                case SDL_SCANCODE_UP:    key = asuka::Key::Up; break;
                case SDL_SCANCODE_DOWN:  key = asuka::Key::Down; break;
                case SDL_SCANCODE_LEFT:  key = asuka::Key::Left; break;
                case SDL_SCANCODE_RIGHT: key = asuka::Key::Right; break;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER: key = asuka::Key::Start; break;
                case SDL_SCANCODE_BACKSPACE: key = asuka::Key::Select; break;
                case SDL_SCANCODE_SPACE: key = asuka::Key::A; break;
                case SDL_SCANCODE_Z:     key = asuka::Key::B; break;
                case SDL_SCANCODE_X:     key = asuka::Key::X; break;
                case SDL_SCANCODE_C:     key = asuka::Key::Y; break;
                default: break;
            }
            if (key != asuka::Key::None && g_state.engine != nullptr) {
                g_state.engine->send_input(static_cast<uint32_t>(key), pressed);
            }
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                   event.type == SDL_CONTROLLERBUTTONUP) {
            const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
            const asuka::Key key = key_from_button(event.cbutton.button);
            if (key != asuka::Key::None && g_state.engine != nullptr) {
                g_state.engine->send_input(static_cast<uint32_t>(key), pressed);
            }
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            if (g_state.pad == nullptr) {
                g_state.pad = SDL_GameControllerOpen(event.cdevice.which);
            }
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (g_state.pad != nullptr &&
                event.cdevice.which ==
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(g_state.pad))) {
                SDL_GameControllerClose(g_state.pad);
                g_state.pad = nullptr;
            }
        }
    }

    if (g_state.pad != nullptr) {
        send_dirs(directions_from_pad(g_state.pad));
    }
}

void present_frame(const uint32_t* pixels, int width, int height) {
    if (g_state.renderer == nullptr) {
        return;
    }
    if (g_state.texture == nullptr) {
        g_state.texture = SDL_CreateTexture(
            g_state.renderer, SDL_PIXELFORMAT_XRGB8888,
            SDL_TEXTUREACCESS_STREAMING, width, height);
        if (g_state.texture == nullptr) {
            return;
        }
    } else {
        int tw = 0;
        int th = 0;
        SDL_QueryTexture(g_state.texture, nullptr, nullptr, &tw, &th);
        if (tw != width || th != height) {
            SDL_DestroyTexture(g_state.texture);
            g_state.texture = SDL_CreateTexture(
                g_state.renderer, SDL_PIXELFORMAT_XRGB8888,
                SDL_TEXTUREACCESS_STREAMING, width, height);
            if (g_state.texture == nullptr) {
                return;
            }
        }
    }

    const int pitch = width * static_cast<int>(sizeof(uint32_t));
    SDL_UpdateTexture(g_state.texture, nullptr, pixels, pitch);
    SDL_RenderSetLogicalSize(g_state.renderer, width, height);
    SDL_RenderClear(g_state.renderer);
    SDL_RenderCopy(g_state.renderer, g_state.texture, nullptr, nullptr);
    SDL_RenderPresent(g_state.renderer);
}

// One emulated frame. Called by Emscripten on every animation frame.
void loop_iteration() {
    if (!g_state.running || g_state.engine == nullptr) {
        return;
    }

    handle_events();
    g_state.engine->step_frame();

    int width = 0;
    int height = 0;
    const uint32_t* pixels = g_state.engine->get_framebuffer(width, height);
    if (pixels != nullptr && width > 0 && height > 0) {
        present_frame(pixels, width, height);
    }

    if (g_state.audio_enabled && g_state.audio_device != 0) {
        size_t sample_count = 0;
        const int16_t* samples = g_state.engine->get_audio_samples(sample_count);
        if (samples != nullptr && sample_count > 0) {
            const int bytes_per_frame = kAudioChannels * 2;
            const Uint32 target_bytes = static_cast<Uint32>(
                static_cast<long long>(kAudioTargetMs) * kAudioSampleRate / 1000 *
                bytes_per_frame);
            if (SDL_GetQueuedAudioSize(g_state.audio_device) < target_bytes) {
                SDL_QueueAudio(g_state.audio_device, samples,
                               static_cast<Uint32>(sample_count * bytes_per_frame));
            }
        }
    }

    ++g_state.frames;
}

SDL_AudioDeviceID open_audio() {
    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = kAudioSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = static_cast<Uint8>(kAudioChannels);
    want.samples = 1024;
    want.callback = nullptr;  // use SDL_QueueAudio()

    const SDL_AudioDeviceID device =
        SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device == 0) {
        std::fprintf(stderr, "asuka/web: no audio device (%s)\n", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(device, 0);
    return device;
}

// Slot paths: slots are 1-based; the state file is "slotN.state" and the
// visual thumbnail "slotN.bmp", both under the persistent snapshot directory.
std::string snapshot_state_path(int slot) {
    return std::string(kSnapshotDir) + "/slot" + std::to_string(slot) + ".state";
}

std::string snapshot_thumb_path(int slot) {
    return std::string(kSnapshotDir) + "/slot" + std::to_string(slot) + ".bmp";
}

// The shell picks the machine by name; FM Towns is the default so existing
// links keep working.
bool is_pc98(const std::string& name) {
    return name == "pc98" || name == "pc-98" || name == "pc9821" ||
           name == "np2" || name == "np2kai";
}

// Allocate the engine the shell asked for. The PC-98 core is only compiled in
// when the NP2kai submodule is present.
asuka::IEngineCore* create_selected_engine(const std::string& name) {
#if defined(ASUKA_WEB_HAS_PC98)
    if (is_pc98(name)) {
        return create_pc98_engine();
    }
#endif
    return create_engine();
}

void destroy_selected_engine(asuka::IEngineCore* engine, const std::string& name) {
#if defined(ASUKA_WEB_HAS_PC98)
    if (is_pc98(name)) {
        destroy_pc98_engine(engine);
        return;
    }
#endif
    (void)name;
    destroy_engine(engine);
}

// Create the requested engine, run init() and (optionally) mount media. On any
// failure the partially built engine is torn down and nullptr is returned, so
// callers never have to clean up after it. The SDL window/renderer/audio and
// the gamepad are owned by the process, not the engine.
asuka::IEngineCore* start_engine(const std::string& machine,
                                 const std::string& media_path) {
    asuka::IEngineCore* engine = create_selected_engine(machine);
    if (engine == nullptr) {
        set_error("engine allocation failed");
        return nullptr;
    }

    if (!engine->init("", g_state.bios_dir)) {
        set_error("engine init() failed (missing or invalid BIOS?)");
        destroy_selected_engine(engine, machine);
        return nullptr;
    }

    if (!media_path.empty() && !engine->load_media(media_path)) {
        set_error("load_media() failed: " + media_path);
        engine->shutdown();
        destroy_selected_engine(engine, machine);
        return nullptr;
    }

    return engine;
}

// Drop the running engine, if any. The shell may call this while the main loop
// is live (a runtime machine swap); the loop tolerates a null engine.
void stop_engine() {
    if (g_state.engine != nullptr) {
        g_state.engine->shutdown();
        destroy_selected_engine(g_state.engine, g_state.machine);
        g_state.engine = nullptr;
    }
    if (g_state.audio_device != 0) {
        // Drop any PCM the old machine queued so the swap does not leak a few
        // milliseconds of the previous engine's audio.
        SDL_ClearQueuedAudio(g_state.audio_device);
    }
}

// Bring up the process-wide SDL platform (window, renderer, audio, gamepad)
// once. The platform outlives any single engine, so a runtime machine swap
// (asuka_web_load_rom) keeps it and only replaces the engine. Returns false and
// sets the error string on failure.
bool ensure_platform() {
    if (g_state.window != nullptr) {
        return true;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_EVENTS) != 0) {
        set_error(std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }

    // SDL_CreateWindow uses Module.canvas when the shell sets it.
    g_state.window = SDL_CreateWindow("Asuka", SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED, 640, 480,
                                      SDL_WINDOW_RESIZABLE);
    if (g_state.window == nullptr) {
        set_error(std::string("cannot create window: ") + SDL_GetError());
        return false;
    }

    g_state.renderer = SDL_CreateRenderer(g_state.window, -1,
                                          SDL_RENDERER_ACCELERATED);
    if (g_state.renderer == nullptr) {
        g_state.renderer = SDL_CreateRenderer(g_state.window, -1, 0);
    }
    if (g_state.renderer == nullptr) {
        set_error(std::string("cannot create renderer: ") + SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetRenderDrawColor(g_state.renderer, 0, 0, 0, 255);

    g_state.audio_device = open_audio();

    // Open the first available game controller, if any.
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) != 0) {
            g_state.pad = SDL_GameControllerOpen(i);
            if (g_state.pad != nullptr) {
                break;
            }
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// C API called from the JavaScript shell.
// ---------------------------------------------------------------------------

extern "C" {

// Initialise the engine and open the canvas/audio. `bios_dir` is the directory
// holding the firmware ROMs; `media_path` is a file inside the virtual FS;
// `machine` selects the engine ("fmtowns", the default, or "pc98").
// Returns 1 on success, 0 on failure (see asuka_web_last_error()).
EMSCRIPTEN_KEEPALIVE int asuka_web_init(const char* bios_dir,
                                        const char* media_path,
                                        const char* machine) {
    if (g_state.engine != nullptr) {
        set_error("already initialised");
        return 0;
    }

    g_state.machine = machine != nullptr ? machine : "";
    g_state.bios_dir = bios_dir != nullptr ? bios_dir : "";

    if (!ensure_platform()) {
        return 0;
    }

    const std::string media = media_path != nullptr ? media_path : "";
    g_state.engine = start_engine(g_state.machine, media);
    if (g_state.engine == nullptr) {
        return 0;
    }

    g_state.last_error.clear();
    g_state.frames = 0;
    g_state.input_events = 0;
    return 1;
}

// Swap in a game image at runtime. The shell calls this after mounting a
// user-supplied file: it drops the current engine (whatever it was) and starts
// a fresh one with the requested machine and media. The canvas, audio and
// gamepad are kept, so the main loop never stops. Returns 1 on success, 0 on
// failure (see asuka_web_last_error()).
//
// Note: the vendored cores power on only once per process, so the shell only
// uses this for a cold start (no engine yet). A machine change while running is
// handled by reloading the page into a fresh runtime.
EMSCRIPTEN_KEEPALIVE int asuka_web_load_rom(const char* machine,
                                            const char* media_path) {
    if (!ensure_platform()) {
        return 0;
    }

    stop_engine();

    g_state.machine = machine != nullptr ? machine : "";
    const std::string media = media_path != nullptr ? media_path : "";

    g_state.engine = start_engine(g_state.machine, media);
    if (g_state.engine == nullptr) {
        return 0;
    }

    g_state.last_error.clear();
    g_state.frames = 0;
    g_state.input_events = 0;
    return 1;
}

// Start the emulation loop. `fps` of 0 lets requestAnimationFrame pace it.
EMSCRIPTEN_KEEPALIVE void asuka_web_start(int fps) {
    if (g_state.engine == nullptr) {
        set_error("start() called before init()");
        return;
    }
    if (g_state.running) {
        return;
    }
    g_state.running = true;
    // simulate_infinite_loop = 0: the shell calls this after main() has
    // returned, so the function must return control to JavaScript.
    emscripten_set_main_loop(loop_iteration, fps, 0);
}

EMSCRIPTEN_KEEPALIVE void asuka_web_stop() {
    if (!g_state.running) {
        return;
    }
    g_state.running = false;
    emscripten_cancel_main_loop();
}

EMSCRIPTEN_KEEPALIVE void asuka_web_reset() {
    if (g_state.engine != nullptr) {
        g_state.engine->reset();
    }
}

// Feed a key event. `key` is an asuka::Key value (see include/asuka/Input.hpp).
EMSCRIPTEN_KEEPALIVE void asuka_web_key(uint32_t key, int pressed) {
    if (g_state.engine != nullptr) {
        g_state.engine->send_input(key, pressed != 0);
    }
    g_state.input_events++;
}

// Feed a core-native raw key code (PC-98 scancode), for the virtual PC-98
// keyboard overlay's keys the asuka::Key set cannot express. Ignored by cores
// without the concept.
EMSCRIPTEN_KEEPALIVE void asuka_web_scancode(int scancode, int pressed) {
    if (g_state.engine != nullptr) {
        g_state.engine->send_scancode(scancode, pressed != 0);
    }
    g_state.input_events++;
}

// Which engine is running ("fmtowns" or "pc98"); lets the shell pick the right
// on-screen controls.
EMSCRIPTEN_KEEPALIVE int asuka_web_is_pc98() {
    return is_pc98(g_state.machine) ? 1 : 0;
}

// Number of key events accepted since init (diagnostics / test hook).
EMSCRIPTEN_KEEPALIVE int asuka_web_input_events() {
    return static_cast<int>(g_state.input_events);
}

// Save the current machine state to slot N, plus a BMP thumbnail beside it.
// The shell must call FS.syncfs() afterwards to persist to IndexedDB.
EMSCRIPTEN_KEEPALIVE int asuka_web_save_snapshot(int slot) {
    if (g_state.engine == nullptr || slot < 1) {
        set_error("save_snapshot: no engine / bad slot");
        return 0;
    }
    if (!g_state.engine->save_snapshot(snapshot_state_path(slot))) {
        set_error("save_snapshot failed");
        return 0;
    }
    return 1;
}

// Restore slot N. Returns 1 on success, 0 on failure.
EMSCRIPTEN_KEEPALIVE int asuka_web_load_snapshot(int slot) {
    if (g_state.engine == nullptr || slot < 1) {
        set_error("load_snapshot: no engine / bad slot");
        return 0;
    }
    if (!g_state.engine->load_snapshot(snapshot_state_path(slot))) {
        set_error("load_snapshot failed");
        return 0;
    }
    return 1;
}

// Read slot N's thumbnail BMP into the scratch buffer and return a pointer to
// it (0 when there is no thumbnail). The byte count is published through
// asuka_web_snapshot_thumbnail_size(); the pointer is owned by the host and
// valid until the next call.
EMSCRIPTEN_KEEPALIVE const uint8_t* asuka_web_get_snapshot_thumbnail(int slot) {
    g_state.thumbnail.clear();
    if (slot < 1) {
        return nullptr;
    }

    // Emscripten's std::ifstream reads the virtual FS transparently.
    std::FILE* file = std::fopen(snapshot_thumb_path(slot).c_str(), "rb");
    if (file == nullptr) {
        return nullptr;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return nullptr;
    }
    g_state.thumbnail.resize(static_cast<size_t>(size));
    const size_t read = std::fread(g_state.thumbnail.data(), 1,
                                   g_state.thumbnail.size(), file);
    std::fclose(file);
    if (read != g_state.thumbnail.size()) {
        g_state.thumbnail.clear();
        return nullptr;
    }
    return g_state.thumbnail.data();
}

// Byte count of the buffer returned by the last
// asuka_web_get_snapshot_thumbnail() call (0 when there was no thumbnail).
EMSCRIPTEN_KEEPALIVE int asuka_web_snapshot_thumbnail_size() {
    return static_cast<int>(g_state.thumbnail.size());
}

// Report whether a slot has a saved state on disk (drives the UI's "filled"
// thumbnail state after a reload).
EMSCRIPTEN_KEEPALIVE int asuka_web_has_snapshot(int slot) {
    if (slot < 1) {
        return 0;
    }
    std::FILE* file = std::fopen(snapshot_state_path(slot).c_str(), "rb");
    if (file == nullptr) {
        return 0;
    }
    std::fclose(file);
    return 1;
}

EMSCRIPTEN_KEEPALIVE int asuka_web_running() {
    return g_state.running ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int asuka_web_frames() {
    return static_cast<int>(g_state.frames);
}

EMSCRIPTEN_KEEPALIVE const char* asuka_web_last_error() {
    return g_state.last_error.c_str();
}

// Tear everything down. Safe to call more than once.
EMSCRIPTEN_KEEPALIVE void asuka_web_shutdown() {
    asuka_web_stop();

    stop_engine();
    if (g_state.pad != nullptr) {
        SDL_GameControllerClose(g_state.pad);
        g_state.pad = nullptr;
    }
    if (g_state.audio_device != 0) {
        SDL_CloseAudioDevice(g_state.audio_device);
        g_state.audio_device = 0;
    }
    if (g_state.texture != nullptr) {
        SDL_DestroyTexture(g_state.texture);
        g_state.texture = nullptr;
    }
    if (g_state.renderer != nullptr) {
        SDL_DestroyRenderer(g_state.renderer);
        g_state.renderer = nullptr;
    }
    if (g_state.window != nullptr) {
        SDL_DestroyWindow(g_state.window);
        g_state.window = nullptr;
    }
    SDL_Quit();
}

}  // extern "C"

int main() {
    // The shell drives everything through the exported API; there is no
    // command line here. Returning keeps the runtime alive (EXIT_RUNTIME=0).
    std::printf("Asuka web runtime ready\n");
    return 0;
}
