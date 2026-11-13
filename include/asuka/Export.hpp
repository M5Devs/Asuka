#pragma once

// Platform export/import macros for plugin symbols.
//
// A plugin is built as a shared library and must export its factory entry
// points.  Consumers (the runner) may optionally import them, though the
// loader normally resolves symbols dynamically via dlsym/GetProcAddress.

#if defined(_WIN32) || defined(__CYGWIN__)
#define ASUKA_PLATFORM_WINDOWS 1
#endif

#if defined(ASUKA_PLATFORM_WINDOWS)
#define ASUKA_EXPORT __declspec(dllexport)
#define ASUKA_IMPORT __declspec(dllimport)
#define ASUKA_LOCAL
#else
#define ASUKA_EXPORT __attribute__((visibility("default")))
#define ASUKA_IMPORT __attribute__((visibility("default")))
#define ASUKA_LOCAL __attribute__((visibility("hidden")))
#endif

// Convenience macro for the exported C entry points of a plugin.
#define ASUKA_PLUGIN_API extern "C" ASUKA_EXPORT
