#include <new>

#include "X68kEngine.hpp"
#include "x68k_plugin.hpp"

#include "asuka/Export.hpp"

// Factory entry points for the Sharp X68000 engine. The desktop plugin loader
// looks for the generic create_engine/destroy_engine names, so the shared
// plugin exports those; the X68000-specific names are always available for
// callers that link the engine in directly (the web build, which also links
// the FM Towns and PC-98 engines and cannot use the generic names twice).
ASUKA_PLUGIN_API asuka::IEngineCore* create_x68k_engine() {
    return new (std::nothrow) asuka::plugins::x68k::X68kEngine();
}

ASUKA_PLUGIN_API void destroy_x68k_engine(asuka::IEngineCore* engine) {
    delete engine;
}

#if defined(ASUKA_PLUGIN_SHARED)
ASUKA_PLUGIN_API asuka::IEngineCore* create_engine() {
    return create_x68k_engine();
}

ASUKA_PLUGIN_API void destroy_engine(asuka::IEngineCore* engine) {
    destroy_x68k_engine(engine);
}
#endif
