#pragma once

#include <string>

#include "asuka/IEngineCore.hpp"

namespace asuka {

// Cross-platform dynamic library loader for engine plugins.
//
// Wraps dlopen/dlsym/dlclose on POSIX (Linux/macOS) and
// LoadLibrary/GetProcAddress/FreeLibrary on Windows. The loader owns the
// library handle and resolves the create_engine/destroy_engine factory pair;
// it does not own the engine instances those factories return (see
// EngineInstance for RAII ownership).
//
// Move-only. On destruction any still-loaded library is unloaded.
class PluginLoader {
public:
    PluginLoader() = default;
    ~PluginLoader();

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&& other) noexcept;
    PluginLoader& operator=(PluginLoader&& other) noexcept;

    // Load the shared library at `path` and resolve its factory symbols.
    // Returns false on failure; if `error` is non-null it receives a
    // human-readable reason.
    bool load(const std::string& path, std::string* error = nullptr);

    // Unload the library if loaded. Any engine created from it must already
    // have been destroyed. Safe to call repeatedly.
    void unload();

    bool is_loaded() const { return handle_ != nullptr; }

    // Path the loader was asked to open.
    const std::string& path() const { return path_; }

    // Instantiate a new engine via the plugin's create_engine(). Returns
    // nullptr if the library is not loaded or the plugin declines.
    IEngineCore* create_engine() const;

    // Destroy an engine previously returned by create_engine().
    void destroy_engine(IEngineCore* engine) const;

    // Build the conventional shared-library filename for `base` on the host
    // platform, e.g. ("dummy_engine") -> "libdummy_engine.so" on Linux,
    // "dummy_engine.dll" on Windows, "libdummy_engine.dylib" on macOS.
    static std::string library_name(const std::string& base);

    // Host default suffix ("so", "dylib" or "dll").
    static const char* library_suffix();

private:
    void* handle_ = nullptr;
    CreateEngineFunc create_ = nullptr;
    DestroyEngineFunc destroy_ = nullptr;
    std::string path_;
};

// RAII owner for an engine instance produced by a PluginLoader.
//
// Guarantees the engine is shut down and destroyed exactly once, and must be
// reset (or destroyed) before the originating PluginLoader is unloaded.
class EngineInstance {
public:
    EngineInstance() = default;
    EngineInstance(PluginLoader* loader, IEngineCore* engine)
        : loader_(loader), engine_(engine) {}

    ~EngineInstance() { reset(); }

    EngineInstance(const EngineInstance&) = delete;
    EngineInstance& operator=(const EngineInstance&) = delete;

    EngineInstance(EngineInstance&& other) noexcept
        : loader_(other.loader_), engine_(other.engine_) {
        other.loader_ = nullptr;
        other.engine_ = nullptr;
    }

    EngineInstance& operator=(EngineInstance&& other) noexcept {
        if (this != &other) {
            reset();
            loader_ = other.loader_;
            engine_ = other.engine_;
            other.loader_ = nullptr;
            other.engine_ = nullptr;
        }
        return *this;
    }

    // Shut the engine down and destroy it, if owned.
    void reset() {
        if (engine_ != nullptr) {
            engine_->shutdown();
        }
        if (engine_ != nullptr && loader_ != nullptr) {
            loader_->destroy_engine(engine_);
        }
        engine_ = nullptr;
        loader_ = nullptr;
    }

    IEngineCore* get() const { return engine_; }
    IEngineCore* operator->() const { return engine_; }
    explicit operator bool() const { return engine_ != nullptr; }

private:
    PluginLoader* loader_ = nullptr;
    IEngineCore* engine_ = nullptr;
};

}  // namespace asuka
