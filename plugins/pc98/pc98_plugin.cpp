#include <new>

#include "PC98Engine.hpp"
#include "pc98_plugin.hpp"

#include "asuka/Export.hpp"

// Factory entry points for the PC-98 engine. The desktop plugin loader looks
// for the generic create_engine/destroy_engine names, so the shared plugin
// exports those; the PC-98-specific names are always available for callers
// that link the engine in directly (the web build, which also links the FM
// Towns engine and cannot use the generic names twice).
ASUKA_PLUGIN_API asuka::IEngineCore* create_pc98_engine() {
    return new (std::nothrow) asuka::plugins::pc98::PC98Engine();
}

ASUKA_PLUGIN_API void destroy_pc98_engine(asuka::IEngineCore* engine) {
    delete engine;
}

#if defined(ASUKA_PLUGIN_SHARED)
ASUKA_PLUGIN_API asuka::IEngineCore* create_engine() {
    return create_pc98_engine();
}

ASUKA_PLUGIN_API void destroy_engine(asuka::IEngineCore* engine) {
    destroy_pc98_engine(engine);
}
#endif
