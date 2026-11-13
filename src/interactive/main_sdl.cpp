// Asuka interactive desktop runner (SDL2).
//
// Loads an engine plugin and runs it in real time in a window, with live audio
// playback and keyboard/gamepad input. This is the "play a game" counterpart to
// the headless asuka_runner, which only advances frames and reports state.
//
// The host owns the main loop, exactly as the plugin ABI intends: it steps one
// frame, then pulls the framebuffer and audio, uploads the frame to an SDL
// texture and queues the audio. Frame pacing follows the audio clock so sound
// stays continuous; the window is only a presentation detail.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "asuka/IEngineCore.hpp"
#include "asuka/Input.hpp"
#include "asuka/PluginLoader.hpp"

#include <SDL.h>

namespace {

// Engine audio format (see IEngineCore): interleaved stereo s16 at 44100 Hz.
constexpr int kAudioSampleRate = 44100;
constexpr int kAudioChannels = 2;
constexpr int kAudioBytesPerSample = 2;

// Default window size. The emulated frame is usually smaller; the renderer
// scales it to fit while preserving aspect ratio.
constexpr int kDefaultWindowWidth = 640;
constexpr int kDefaultWindowHeight = 480;

// How much audio to keep queued ahead of playback. Enough to ride out frame
// jitter, small enough that input and video stay close to the sound.
constexpr int kAudioTargetMs = 80;

// Fallback video rate when audio is unavailable and cannot pace the loop.
constexpr double kFallbackFps = 60.0;

// Left-stick deflection beyond which a direction registers.
constexpr Sint16 kStickDeadzone = 8000;

struct Options {
    std::string plugin;
    std::string system;
    std::string bios;
    std::string media;
    // 0 means run until the user quits.
    int frames = 0;
    int window_width = kDefaultWindowWidth;
    int window_height = kDefaultWindowHeight;
    bool vsync = false;
    bool headless = false;
    bool no_audio = false;
    bool integer_scale = true;
    // Single snapshot slot: F5 saves the machine here, F8 restores it. Defaults
    // to "<media>.state" (with a BMP thumbnail beside it) once media is known.
    std::string snapshot;
};

void print_usage(const char* argv0) {
    std::fprintf(
        stderr,
        "Usage: %s --plugin <path> --bios <dir> --media <path> [options]\n"
        "  --plugin <path>       engine plugin shared library to load\n"
        "  --system <dir>        engine system/asset directory (init arg 1)\n"
        "  --bios <dir>          directory with BIOS/ROM files (init arg 2)\n"
        "  --media <path>        media image (CHD, CUE/BIN, ISO, floppy)\n"
        "  --frames <N>          stop after N frames (default: run until quit)\n"
        "  --width <W>           initial window width (default %d)\n"
        "  --height <H>          initial window height (default %d)\n"
        "  --vsync               additionally sync presentation to the display\n"
        "  --no-integer-scale   allow fractional scaling instead of integers\n"
        "  --headless            no window; still runs audio and the loop\n"
        "  --no-audio            do not open an audio device\n"
        "  --snapshot <path>     state file for F5 (save) / F8 (load)\n"
        "  -h, --help            show this help\n"
        "\n"
        "Controls: arrows = d-pad, Enter = Start, Space/Z/X/C = fire buttons,\n"
        "R = reset, F11 = fullscreen, F5 = save state, F8 = load state,\n"
        "Esc = quit. A gamepad is used if present.\n",
        argv0, kDefaultWindowWidth, kDefaultWindowHeight);
}

bool take_value(int argc, char** argv, int& i, std::string& out) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "error: option '%s' requires a value\n", argv[i]);
        return false;
    }
    out = argv[++i];
    return true;
}

bool parse_args(int argc, char** argv, Options& opts, bool& help) {
    help = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            help = true;
            return true;
        } else if (arg == "--plugin") {
            if (!take_value(argc, argv, i, opts.plugin)) return false;
        } else if (arg == "--system") {
            if (!take_value(argc, argv, i, opts.system)) return false;
        } else if (arg == "--bios") {
            if (!take_value(argc, argv, i, opts.bios)) return false;
        } else if (arg == "--media") {
            if (!take_value(argc, argv, i, opts.media)) return false;
        } else if (arg == "--frames") {
            std::string value;
            if (!take_value(argc, argv, i, value)) return false;
            opts.frames = std::atoi(value.c_str());
            if (opts.frames < 0) {
                std::fprintf(stderr, "error: --frames must be >= 0\n");
                return false;
            }
        } else if (arg == "--width") {
            std::string value;
            if (!take_value(argc, argv, i, value)) return false;
            opts.window_width = std::atoi(value.c_str());
            if (opts.window_width <= 0) {
                std::fprintf(stderr, "error: --width must be positive\n");
                return false;
            }
        } else if (arg == "--height") {
            std::string value;
            if (!take_value(argc, argv, i, value)) return false;
            opts.window_height = std::atoi(value.c_str());
            if (opts.window_height <= 0) {
                std::fprintf(stderr, "error: --height must be positive\n");
                return false;
            }
        } else if (arg == "--vsync") {
            opts.vsync = true;
        } else if (arg == "--no-integer-scale") {
            opts.integer_scale = false;
        } else if (arg == "--headless") {
            opts.headless = true;
        } else if (arg == "--no-audio") {
            opts.no_audio = true;
        } else if (arg == "--snapshot") {
            if (!take_value(argc, argv, i, opts.snapshot)) return false;
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
            return false;
        }
    }
    return true;
}

// Map a keyboard scancode to a host key. Returns Key::None for keys we do not
// forward to the engine (those are handled by the host itself, e.g. Esc).
asuka::Key key_from_scancode(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_UP:    return asuka::Key::Up;
        case SDL_SCANCODE_DOWN:  return asuka::Key::Down;
        case SDL_SCANCODE_LEFT:  return asuka::Key::Left;
        case SDL_SCANCODE_RIGHT: return asuka::Key::Right;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return asuka::Key::Start;
        case SDL_SCANCODE_BACKSPACE: return asuka::Key::Select;
        case SDL_SCANCODE_SPACE: return asuka::Key::A;
        case SDL_SCANCODE_Z:     return asuka::Key::B;
        case SDL_SCANCODE_X:     return asuka::Key::X;
        case SDL_SCANCODE_C:     return asuka::Key::Y;
        default:                 return asuka::Key::None;
    }
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

// The four directional inputs as a bitmask, so the analog stick and the d-pad
// can be combined and edge-detected together.
enum Direction : unsigned {
    kDirUp = 1u << 0,
    kDirDown = 1u << 1,
    kDirLeft = 1u << 2,
    kDirRight = 1u << 3,
};

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

// A small RAII wrapper around the SDL objects the runner owns, so every exit
// path tears them down in the right order (engine first, then SDL).
struct SdlContext {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioDeviceID audio_device = 0;
    SDL_GameController* pad = nullptr;

    void destroy() {
        if (pad != nullptr) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
        }
        if (audio_device != 0) {
            SDL_CloseAudioDevice(audio_device);
            audio_device = 0;
        }
        if (texture != nullptr) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
    }
};

// Open the audio device and start playback. Returns the device id, or 0 on
// failure (the caller continues without audio).
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
        std::fprintf(stderr, "warning: no audio device (%s); running silent\n",
                     SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(device, 0);
    return device;
}

bool open_video(SdlContext& sdl, const Options& opts) {
    sdl.window = SDL_CreateWindow(
        "Asuka", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        opts.window_width, opts.window_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (sdl.window == nullptr) {
        std::fprintf(stderr, "error: cannot create window: %s\n", SDL_GetError());
        return false;
    }

    Uint32 flags = SDL_RENDERER_ACCELERATED;
    if (opts.vsync) {
        flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    sdl.renderer = SDL_CreateRenderer(sdl.window, -1, flags);
    if (sdl.renderer == nullptr) {
        // Fall back to software rendering rather than refusing to start.
        sdl.renderer = SDL_CreateRenderer(sdl.window, -1, 0);
    }
    if (sdl.renderer == nullptr) {
        std::fprintf(stderr, "error: cannot create renderer: %s\n", SDL_GetError());
        return false;
    }

    // Nearest-neighbour keeps the pixel art crisp when scaled up.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (opts.integer_scale) {
        SDL_RenderSetIntegerScale(sdl.renderer, SDL_TRUE);
    }
    SDL_SetRenderDrawColor(sdl.renderer, 0, 0, 0, 255);
    return true;
}

// Present one frame: recreate the texture if the frame size changed, upload
// the pixels, and draw letterboxed to preserve aspect ratio.
void present_frame(SdlContext& sdl, const uint32_t* pixels, int width,
                   int height) {
    if (sdl.texture == nullptr) {
        sdl.texture = SDL_CreateTexture(sdl.renderer, SDL_PIXELFORMAT_XRGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, width, height);
        if (sdl.texture == nullptr) {
            return;
        }
    } else {
        int tw = 0;
        int th = 0;
        SDL_QueryTexture(sdl.texture, nullptr, nullptr, &tw, &th);
        if (tw != width || th != height) {
            SDL_DestroyTexture(sdl.texture);
            sdl.texture = SDL_CreateTexture(
                sdl.renderer, SDL_PIXELFORMAT_XRGB8888,
                SDL_TEXTUREACCESS_STREAMING, width, height);
            if (sdl.texture == nullptr) {
                return;
            }
        }
    }

    // The engine's buffer is tightly packed at width pixels per row.
    const int pitch = width * static_cast<int>(sizeof(uint32_t));
    SDL_UpdateTexture(sdl.texture, nullptr, pixels, pitch);

    SDL_RenderSetLogicalSize(sdl.renderer, width, height);
    SDL_RenderClear(sdl.renderer);
    SDL_RenderCopy(sdl.renderer, sdl.texture, nullptr, nullptr);
    SDL_RenderPresent(sdl.renderer);
}

// Run the engine loop. Returns true on a clean run.
bool run(const Options& opts) {
    std::string plugin_path = opts.plugin;
    if (plugin_path.empty()) {
        std::fprintf(stderr, "error: --plugin is required\n");
        return false;
    }

    std::printf("Asuka interactive runner\n");
    std::printf("  plugin : %s\n", plugin_path.c_str());
    std::printf("  bios   : %s\n",
                opts.bios.empty() ? "(none)" : opts.bios.c_str());
    std::printf("  media  : %s\n",
                opts.media.empty() ? "(none)" : opts.media.c_str());

    const Uint32 sdl_flags =
        (opts.headless ? 0u : static_cast<Uint32>(SDL_INIT_VIDEO)) |
        (opts.no_audio ? 0u : static_cast<Uint32>(SDL_INIT_AUDIO)) |
        static_cast<Uint32>(SDL_INIT_EVENTS) |
        static_cast<Uint32>(SDL_INIT_GAMECONTROLLER);
    if (SDL_Init(sdl_flags) != 0) {
        std::fprintf(stderr, "error: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SdlContext sdl;
    if (!opts.headless && !open_video(sdl, opts)) {
        SDL_Quit();
        return false;
    }
    if (!opts.no_audio) {
        sdl.audio_device = open_audio();
    }

    // Open the first available game controller, if any.
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) != 0) {
            sdl.pad = SDL_GameControllerOpen(i);
            if (sdl.pad != nullptr) {
                std::printf("  gamepad: %s\n", SDL_GameControllerName(sdl.pad));
                break;
            }
        }
    }

    asuka::PluginLoader loader;
    std::string error;
    if (!loader.load(plugin_path, &error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        sdl.destroy();
        SDL_Quit();
        return false;
    }

    asuka::EngineInstance engine(&loader, loader.create_engine());
    if (!engine) {
        std::fprintf(stderr, "error: plugin returned no engine instance\n");
        sdl.destroy();
        SDL_Quit();
        return false;
    }

    if (!engine->init(opts.system, opts.bios)) {
        std::fprintf(stderr, "error: engine init() failed\n");
        sdl.destroy();
        SDL_Quit();
        return false;
    }
    if (!opts.media.empty() && !engine->load_media(opts.media)) {
        std::fprintf(stderr, "error: engine load_media() failed\n");
        sdl.destroy();
        SDL_Quit();
        return false;
    }

    std::printf("Running. Esc quits, R resets, F11 toggles fullscreen.\n");

    // Resolve the snapshot slot: explicit --snapshot, else "<media>.state".
    std::string snapshot_path = opts.snapshot;
    if (snapshot_path.empty() && !opts.media.empty()) {
        snapshot_path = opts.media;
        const std::string::size_type dot = snapshot_path.find_last_of('.');
        const std::string::size_type slash = snapshot_path.find_last_of("/\\");
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash)) {
            snapshot_path.erase(dot);
        }
        snapshot_path += ".state";
    }
    if (!snapshot_path.empty()) {
        std::printf("Snapshots: F5 saves, F8 loads (%s)\n", snapshot_path.c_str());
    }

    const Uint32 max_queued =
        static_cast<Uint32>(static_cast<int64_t>(kAudioTargetMs) *
                            kAudioSampleRate / 1000 * kAudioChannels *
                            kAudioBytesPerSample);

    bool running = true;
    int frame = 0;
    int width = 0;
    int height = 0;
    unsigned prev_dirs = 0;
    Uint64 last_stats = SDL_GetTicks64();
    Uint64 last_frame_tick = SDL_GetTicks64();
    int frames_since_stats = 0;
    double fps = 0.0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        running = false;
                        break;
                    }
                    if (event.key.keysym.scancode == SDL_SCANCODE_F11) {
                        if (sdl.window != nullptr) {
                            const Uint32 fs = SDL_GetWindowFlags(sdl.window);
                            SDL_SetWindowFullscreen(
                                sdl.window,
                                (fs & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0
                                    ? 0
                                    : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        }
                        break;
                    }
                    if (event.key.keysym.scancode == SDL_SCANCODE_R) {
                        engine->reset();
                        break;
                    }
                    if (event.key.keysym.scancode == SDL_SCANCODE_F5 &&
                        !snapshot_path.empty()) {
                        std::printf(engine->save_snapshot(snapshot_path)
                                        ? "Snapshot saved to %s\n"
                                        : "Snapshot save FAILED (%s)\n",
                                    snapshot_path.c_str());
                        break;
                    }
                    if (event.key.keysym.scancode == SDL_SCANCODE_F8 &&
                        !snapshot_path.empty()) {
                        std::printf(engine->load_snapshot(snapshot_path)
                                        ? "Snapshot loaded from %s\n"
                                        : "Snapshot load FAILED (%s)\n",
                                    snapshot_path.c_str());
                        break;
                    }
                    if (event.key.repeat == 0) {
                        const asuka::Key key =
                            key_from_scancode(event.key.keysym.scancode);
                        if (key != asuka::Key::None) {
                            engine->send_input(static_cast<uint32_t>(key), true);
                        }
                    }
                    break;
                case SDL_KEYUP: {
                    const asuka::Key key =
                        key_from_scancode(event.key.keysym.scancode);
                    if (key != asuka::Key::None) {
                        engine->send_input(static_cast<uint32_t>(key), false);
                    }
                    break;
                }
                case SDL_CONTROLLERBUTTONDOWN: {
                    const asuka::Key key = key_from_button(event.cbutton.button);
                    if (key != asuka::Key::None) {
                        engine->send_input(static_cast<uint32_t>(key), true);
                    }
                    break;
                }
                case SDL_CONTROLLERBUTTONUP: {
                    const asuka::Key key = key_from_button(event.cbutton.button);
                    if (key != asuka::Key::None) {
                        engine->send_input(static_cast<uint32_t>(key), false);
                    }
                    break;
                }
                case SDL_CONTROLLERDEVICEADDED:
                    if (sdl.pad == nullptr && SDL_IsGameController(event.cdevice.which) != 0) {
                        sdl.pad = SDL_GameControllerOpen(event.cdevice.which);
                    }
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (sdl.pad != nullptr &&
                        event.cdevice.which ==
                            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(sdl.pad))) {
                        SDL_GameControllerClose(sdl.pad);
                        sdl.pad = nullptr;
                    }
                    break;
                default:
                    break;
            }
        }
        if (!running) {
            break;
        }

        // Fold the analog stick into the d-pad and emit edge transitions.
        if (sdl.pad != nullptr) {
            const unsigned dirs = directions_from_pad(sdl.pad);
            if ((dirs & kDirUp) != (prev_dirs & kDirUp)) {
                engine->send_input(static_cast<uint32_t>(asuka::Key::Up),
                                   (dirs & kDirUp) != 0);
            }
            if ((dirs & kDirDown) != (prev_dirs & kDirDown)) {
                engine->send_input(static_cast<uint32_t>(asuka::Key::Down),
                                   (dirs & kDirDown) != 0);
            }
            if ((dirs & kDirLeft) != (prev_dirs & kDirLeft)) {
                engine->send_input(static_cast<uint32_t>(asuka::Key::Left),
                                   (dirs & kDirLeft) != 0);
            }
            if ((dirs & kDirRight) != (prev_dirs & kDirRight)) {
                engine->send_input(static_cast<uint32_t>(asuka::Key::Right),
                                   (dirs & kDirRight) != 0);
            }
            prev_dirs = dirs;
        }

        engine->step_frame();

        const uint32_t* framebuffer = engine->get_framebuffer(width, height);
        if (framebuffer != nullptr && width > 0 && height > 0) {
            if (!opts.headless) {
                present_frame(sdl, framebuffer, width, height);
            }
        }

        size_t sample_count = 0;
        const int16_t* audio = engine->get_audio_samples(sample_count);
        if (sdl.audio_device != 0 && audio != nullptr && sample_count > 0) {
            const size_t values = sample_count * kAudioChannels;
            const Uint32 bytes =
                static_cast<Uint32>(values * sizeof(int16_t));
            SDL_QueueAudio(sdl.audio_device, audio, bytes);

            // Pace to the audio clock: if we are running ahead of playback
            // (the queue is deeper than the target latency), sleep off the
            // excess in one go. If the queue is empty we are behind real time,
            // so we keep stepping to catch up. This keeps sound continuous
            // while the video tracks the machine's true rate.
            const Uint32 queued = SDL_GetQueuedAudioSize(sdl.audio_device);
            if (queued > max_queued) {
                const double bytes_per_ms =
                    static_cast<double>(kAudioSampleRate) * kAudioChannels *
                    kAudioBytesPerSample / 1000.0;
                const Uint32 wait_ms = static_cast<Uint32>(
                    static_cast<double>(queued - max_queued) / bytes_per_ms);
                if (wait_ms > 0) {
                    SDL_Delay(wait_ms);
                }
            }
        } else if (!opts.headless) {
            // No audio clock: pace the loop to a nominal video rate.
            const Uint64 now = SDL_GetTicks64();
            const Uint64 target = static_cast<Uint64>(1000.0 / kFallbackFps);
            const Uint64 elapsed = now - last_frame_tick;
            if (elapsed < target) {
                SDL_Delay(static_cast<Uint32>(target - elapsed));
            }
            last_frame_tick = SDL_GetTicks64();
        }

        ++frame;
        ++frames_since_stats;
        const Uint64 now = SDL_GetTicks64();
        if (now - last_stats >= 1000) {
            fps = static_cast<double>(frames_since_stats) * 1000.0 /
                  static_cast<double>(now - last_stats);
            last_stats = now;
            frames_since_stats = 0;
            const Uint32 queued = sdl.audio_device != 0
                                      ? SDL_GetQueuedAudioSize(sdl.audio_device)
                                      : 0;
            std::printf("  %dx%d  %.1f fps  audio queue %u B  frame %d\n",
                        width, height, fps, queued, frame);
            std::fflush(stdout);
        }

        if (opts.frames > 0 && frame >= opts.frames) {
            break;
        }
    }

    // EngineInstance::reset() shuts the engine down before destroying it and
    // clearing the owner, so this is the whole teardown.
    engine.reset();
    loader.unload();
    sdl.destroy();
    SDL_Quit();

    std::printf("Ran %d frames (%.1f fps). Clean shutdown.\n", frame, fps);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    bool help = false;
    if (!parse_args(argc, argv, opts, help)) {
        return EXIT_FAILURE;
    }
    if (help) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    return run(opts) ? EXIT_SUCCESS : EXIT_FAILURE;
}
