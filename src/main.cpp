// Asuka CLI test runner.
//
// Loads an engine plugin shared library, runs a fixed number of frames and
// reports framebuffer/audio state, then tears everything down. Used to prove
// the plugin ABI, the loader and the runner all work together.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "asuka/IEngineCore.hpp"
#include "asuka/PluginLoader.hpp"

namespace {

constexpr int kFramesToRun = 60;

const char* kDefaultPluginBase = "asuka_dummy_engine";

// Best-effort default plugin path: ASUKA_PLUGIN_PATH env var, else the first
// argv entry, else the conventional build-output location.
std::string default_plugin_path() {
    if (const char* env = std::getenv("ASUKA_PLUGIN_PATH")) {
        return env;
    }
    return std::string("build/plugins/dummy/") +
           asuka::PluginLoader::library_name(kDefaultPluginBase);
}

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s [path/to/plugin" "]\n"
                 "  Loads an Asuka engine plugin and runs %d dummy frames.\n"
                 "  If no path is given, ASUKA_PLUGIN_PATH or\n"
                 "  build/plugins/dummy/%s is used.\n",
                 argv0, kFramesToRun,
                 asuka::PluginLoader::library_name(kDefaultPluginBase).c_str());
}

int run(int argc, char** argv) {
    std::string plugin_path = (argc > 1) ? argv[1] : default_plugin_path();

    std::printf("Asuka test runner\n");
    std::printf("  plugin: %s\n", plugin_path.c_str());

    asuka::PluginLoader loader;
    std::string error;
    if (!loader.load(plugin_path, &error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    std::printf("  plugin loaded OK\n");

    asuka::EngineInstance engine(&loader, loader.create_engine());
    if (!engine) {
        std::fprintf(stderr, "error: plugin returned no engine instance\n");
        return EXIT_FAILURE;
    }

    if (!engine->init("systems/dummy", "systems/dummy/bios")) {
        std::fprintf(stderr, "error: engine init() failed\n");
        return EXIT_FAILURE;
    }
    if (!engine->load_media("media/dummy.rom")) {
        std::fprintf(stderr, "error: engine load_media() failed\n");
        return EXIT_FAILURE;
    }

    // Demonstrate the input path once before running frames.
    engine->send_input(/*key_code=*/0x01, /*pressed=*/true);

    int last_width = 0;
    int last_height = 0;
    uint32_t first_center_pixel = 0;
    uint32_t last_center_pixel = 0;
    size_t audio_samples = 0;
    bool audio_seen = false;

    for (int frame = 0; frame < kFramesToRun; ++frame) {
        engine->step_frame();

        int width = 0;
        int height = 0;
        const uint32_t* framebuffer = engine->get_framebuffer(width, height);
        if (framebuffer == nullptr || width <= 0 || height <= 0) {
            std::fprintf(stderr, "error: no framebuffer after frame %d\n",
                         frame);
            return EXIT_FAILURE;
        }
        last_width = width;
        last_height = height;

        const size_t center_index =
            static_cast<size_t>(height / 2) * static_cast<size_t>(width) +
            static_cast<size_t>(width / 2);
        const uint32_t center = framebuffer[center_index];
        if (frame == 0) {
            first_center_pixel = center;
        }
        last_center_pixel = center;

        size_t count = 0;
        const int16_t* audio = engine->get_audio_samples(count);
        if (audio != nullptr && count > 0) {
            audio_samples = count;
            audio_seen = true;
        }

        std::printf(
            "  frame %2d/%d  %dx%d  center=0x%08X  audio=%zu samples/ch\n",
            frame + 1, kFramesToRun, width, height, center, count);
    }

    std::printf("Result:\n");
    std::printf("  frames rendered : %d\n", kFramesToRun);
    std::printf("  framebuffer     : %dx%d XRGB8888\n", last_width,
                last_height);
    std::printf("  center pixel    : 0x%08X -> 0x%08X (%s)\n",
                first_center_pixel, last_center_pixel,
                first_center_pixel != last_center_pixel ? "animating"
                                                        : "static");
    std::printf("  audio buffer    : %s (%zu samples/channel)\n",
                audio_seen ? "present" : "empty", audio_samples);

    if (first_center_pixel == last_center_pixel) {
        std::fprintf(stderr,
                     "error: framebuffer did not change across frames\n");
        return EXIT_FAILURE;
    }
    if (!audio_seen) {
        std::fprintf(stderr, "error: no audio samples produced\n");
        return EXIT_FAILURE;
    }

    // engine (RAII) shuts down + destroys the instance; loader unloads after.
    engine.reset();
    loader.unload();

    std::printf("Teardown complete: engine destroyed, plugin unloaded.\n");
    std::printf("OK\n");
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    return run(argc, argv);
}
