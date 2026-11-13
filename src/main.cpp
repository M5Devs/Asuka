// Asuka CLI runner.
//
// Loads an engine plugin shared library, runs a number of frames and reports
// framebuffer/audio state, optionally dumping the final frame to a PPM for
// inspection without a GUI. Used to prove the plugin ABI, the loader and a
// real engine (FM Towns via Geartowns) all work together.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "asuka/IEngineCore.hpp"
#include "asuka/PluginLoader.hpp"

namespace {

constexpr int kDefaultFrames = 60;
const char* kDefaultPluginBase = "asuka_dummy_engine";

struct Options {
    std::string plugin;
    std::string system;
    std::string bios;
    std::string media;
    int frames = kDefaultFrames;
    std::string dump_frame;
};

void print_usage(const char* argv0) {
    std::fprintf(
        stderr,
        "Usage: %s [options] [plugin-path]\n"
        "  --plugin <path>       engine plugin shared library to load\n"
        "  --system <dir>        engine system/asset directory (init arg 1)\n"
        "  --bios <dir>          directory with BIOS/ROM files (init arg 2)\n"
        "  --media <path>        media image (CHD, CUE/BIN, ISO, floppy)\n"
        "  --frames <N>          frames to run (default %d)\n"
        "  --dump-frame <path>   write the final frame to a binary PPM (P6)\n"
        "  -h, --help            show this help\n"
        "\n"
        "If --plugin is omitted, a positional plugin path is used, then\n"
        "ASUKA_PLUGIN_PATH, then build/plugins/dummy/%s.\n",
        argv0, kDefaultFrames,
        asuka::PluginLoader::library_name(kDefaultPluginBase).c_str());
}

// Returns false if the option requires a value that is missing.
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
    std::vector<std::string> positionals;

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
            if (opts.frames <= 0) {
                std::fprintf(stderr, "error: --frames must be positive\n");
                return false;
            }
        } else if (arg == "--dump-frame") {
            if (!take_value(argc, argv, i, opts.dump_frame)) return false;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
            return false;
        } else {
            positionals.push_back(arg);
        }
    }

    if (opts.plugin.empty() && !positionals.empty()) {
        opts.plugin = positionals.front();
    }
    return true;
}

// Write a 32-bit XRGB8888 buffer as a binary PPM (P6). Geartowns and the
// dummy plugin both store 0xAARRGGBB with the low three bytes as R,G,B.
bool write_ppm(const std::string& path, const uint32_t* pixels, int width,
               int height) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "error: cannot open '%s' for writing\n",
                     path.c_str());
        return false;
    }

    std::fprintf(file, "P6\n%d %d\n255\n", width, height);

    std::vector<uint8_t> row(static_cast<size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        const uint32_t* src =
            pixels + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            const uint32_t px = src[x];
            row[static_cast<size_t>(x) * 3 + 0] = static_cast<uint8_t>(px >> 16);
            row[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>(px >> 8);
            row[static_cast<size_t>(x) * 3 + 2] = static_cast<uint8_t>(px);
        }
        if (std::fwrite(row.data(), 1, row.size(), file) != row.size()) {
            std::fprintf(stderr, "error: short write to '%s'\n", path.c_str());
            std::fclose(file);
            return false;
        }
    }

    std::fclose(file);
    return true;
}

int run(const Options& opts) {
    std::string plugin_path = opts.plugin;
    if (plugin_path.empty()) {
        if (const char* env = std::getenv("ASUKA_PLUGIN_PATH")) {
            plugin_path = env;
        } else {
            plugin_path = std::string("build/plugins/dummy/") +
                          asuka::PluginLoader::library_name(kDefaultPluginBase);
        }
    }

    std::printf("Asuka test runner\n");
    std::printf("  plugin : %s\n", plugin_path.c_str());
    std::printf("  system : %s\n",
                opts.system.empty() ? "(none)" : opts.system.c_str());
    std::printf("  bios   : %s\n",
                opts.bios.empty() ? "(none)" : opts.bios.c_str());
    std::printf("  media  : %s\n",
                opts.media.empty() ? "(none)" : opts.media.c_str());
    std::printf("  frames : %d\n", opts.frames);

    asuka::PluginLoader loader;
    std::string error;
    if (!loader.load(plugin_path, &error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return EXIT_FAILURE;
    }
    std::printf("  plugin loaded OK\n");

    asuka::EngineInstance engine(&loader, loader.create_engine());
    if (!engine) {
        std::fprintf(stderr, "error: plugin returned no engine instance\n");
        return EXIT_FAILURE;
    }

    if (!engine->init(opts.system, opts.bios)) {
        std::fprintf(stderr, "error: engine init() failed\n");
        return EXIT_FAILURE;
    }
    if (!opts.media.empty() && !engine->load_media(opts.media)) {
        std::fprintf(stderr, "error: engine load_media() failed\n");
        return EXIT_FAILURE;
    }

    int width = 0;
    int height = 0;
    size_t audio_samples = 0;
    bool audio_seen = false;
    bool frame_seen = false;
    int report_every = opts.frames / 10;
    if (report_every < 1) {
        report_every = 1;
    }

    for (int frame = 0; frame < opts.frames; ++frame) {
        engine->step_frame();

        int w = 0;
        int h = 0;
        const uint32_t* framebuffer = engine->get_framebuffer(w, h);
        if (framebuffer != nullptr && w > 0 && h > 0) {
            width = w;
            height = h;
            frame_seen = true;
        }

        size_t count = 0;
        const int16_t* audio = engine->get_audio_samples(count);
        if (audio != nullptr && count > 0) {
            audio_samples = count;
            audio_seen = true;
        }

        if (frame == 0 || frame + 1 == opts.frames ||
            ((frame + 1) % report_every) == 0) {
            std::printf("  frame %4d/%d  %dx%d  audio=%zu samples/ch\n",
                        frame + 1, opts.frames, width, height, count);
        }
    }

    if (!frame_seen) {
        std::fprintf(stderr, "error: engine produced no framebuffer\n");
        return EXIT_FAILURE;
    }

    if (!opts.dump_frame.empty()) {
        int w = 0;
        int h = 0;
        const uint32_t* framebuffer = engine->get_framebuffer(w, h);
        if (framebuffer == nullptr) {
            std::fprintf(stderr, "error: cannot dump frame (no framebuffer)\n");
            return EXIT_FAILURE;
        }
        if (!write_ppm(opts.dump_frame, framebuffer, w, h)) {
            return EXIT_FAILURE;
        }
        std::printf("  wrote final frame to %s (%dx%d PPM)\n",
                    opts.dump_frame.c_str(), w, h);
    }

    std::printf("Result:\n");
    std::printf("  frames rendered : %d\n", opts.frames);
    std::printf("  framebuffer     : %dx%d XRGB8888\n", width, height);
    std::printf("  audio buffer    : %s (%zu samples/channel)\n",
                audio_seen ? "present" : "empty", audio_samples);

    engine.reset();
    loader.unload();

    std::printf("Teardown complete: engine destroyed, plugin unloaded.\n");
    std::printf("OK\n");
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    bool help = false;
    if (!parse_args(argc, argv, opts, help)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (help) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    return run(opts);
}
