#include "asuka/PluginLoader.hpp"

#include <utility>

#if defined(_WIN32) || defined(__CYGWIN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "asuka/Export.hpp"

namespace asuka {

namespace {

#if defined(ASUKA_PLATFORM_WINDOWS)
// Translate a Win32 error code into a readable message.
std::string win32_error_message() {
    const DWORD code = GetLastError();
    if (code == 0) {
        return "unknown error";
    }
    char* buffer = nullptr;
    const DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPTSTR>(&buffer), 0, nullptr);
    std::string message;
    if (len != 0 && buffer != nullptr) {
        message.assign(buffer, len);
        while (!message.empty() &&
               (message.back() == '\r' || message.back() == '\n' ||
                message.back() == ' ')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    if (message.empty()) {
        message = "error code " + std::to_string(code);
    }
    return message;
}
#endif

}  // namespace

PluginLoader::~PluginLoader() { unload(); }

PluginLoader::PluginLoader(PluginLoader&& other) noexcept
    : handle_(other.handle_),
      create_(other.create_),
      destroy_(other.destroy_),
      path_(std::move(other.path_)) {
    other.handle_ = nullptr;
    other.create_ = nullptr;
    other.destroy_ = nullptr;
}

PluginLoader& PluginLoader::operator=(PluginLoader&& other) noexcept {
    if (this != &other) {
        unload();
        handle_ = other.handle_;
        create_ = other.create_;
        destroy_ = other.destroy_;
        path_ = std::move(other.path_);
        other.handle_ = nullptr;
        other.create_ = nullptr;
        other.destroy_ = nullptr;
    }
    return *this;
}

bool PluginLoader::load(const std::string& path, std::string* error) {
    const auto fail = [error](const std::string& reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    };

    unload();
    path_ = path;

#if defined(ASUKA_PLATFORM_WINDOWS)
    // Use the ANSI entry point and a conversion-free path; callers pass a
    // plain path. LoadLibraryExA is used so whole-process dependencies are
    // not silently searched.
    HMODULE module = LoadLibraryA(path.c_str());
    if (module == nullptr) {
        return fail("failed to load '" + path + "': " + win32_error_message());
    }
    handle_ = static_cast<void*>(module);

    FARPROC create_sym =
        GetProcAddress(module, ASUKA_CREATE_ENGINE_SYMBOL);
    FARPROC destroy_sym =
        GetProcAddress(module, ASUKA_DESTROY_ENGINE_SYMBOL);
#else
    // RTLD_NOW resolves all symbols eagerly so missing dependencies surface
    // at load time rather than on first call. RTLD_LOCAL keeps the plugin's
    // symbols out of the global namespace.
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        const char* dl_error = dlerror();
        return fail("failed to load '" + path + "': " +
                    (dl_error != nullptr ? dl_error : "unknown error"));
    }
    handle_ = module;

    void* create_sym = dlsym(module, ASUKA_CREATE_ENGINE_SYMBOL);
    void* destroy_sym = dlsym(module, ASUKA_DESTROY_ENGINE_SYMBOL);
#endif

    if (create_sym == nullptr || destroy_sym == nullptr) {
        std::string missing;
        if (create_sym == nullptr) {
            missing = ASUKA_CREATE_ENGINE_SYMBOL;
        }
        if (destroy_sym == nullptr) {
            if (!missing.empty()) {
                missing += ", ";
            }
            missing += ASUKA_DESTROY_ENGINE_SYMBOL;
        }
        unload();
        return fail("plugin '" + path + "' is missing symbol(s): " + missing);
    }

    create_ = reinterpret_cast<CreateEngineFunc>(create_sym);
    destroy_ = reinterpret_cast<DestroyEngineFunc>(destroy_sym);
    return true;
}

void PluginLoader::unload() {
    if (handle_ != nullptr) {
#if defined(ASUKA_PLATFORM_WINDOWS)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }
    handle_ = nullptr;
    create_ = nullptr;
    destroy_ = nullptr;
    path_.clear();
}

IEngineCore* PluginLoader::create_engine() const {
    if (create_ == nullptr) {
        return nullptr;
    }
    return create_();
}

void PluginLoader::destroy_engine(IEngineCore* engine) const {
    if (engine != nullptr && destroy_ != nullptr) {
        destroy_(engine);
    }
}

const char* PluginLoader::library_suffix() {
#if defined(ASUKA_PLATFORM_WINDOWS)
    return "dll";
#elif defined(__APPLE__)
    return "dylib";
#else
    return "so";
#endif
}

std::string PluginLoader::library_name(const std::string& base) {
#if defined(ASUKA_PLATFORM_WINDOWS)
    // Windows shared libraries carry no "lib" prefix and use a .dll suffix.
    return base + ".dll";
#elif defined(__APPLE__)
    return "lib" + base + ".dylib";
#else
    return "lib" + base + ".so";
#endif
}

}  // namespace asuka
