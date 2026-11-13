#include <new>

#include "FMTownsEngine.hpp"

#include "asuka/Export.hpp"

// Exported C entry points resolved by the Asuka plugin loader.
ASUKA_PLUGIN_API asuka::IEngineCore* create_engine() {
    return new (std::nothrow) asuka::plugins::fmtowns::FMTownsEngine();
}

ASUKA_PLUGIN_API void destroy_engine(asuka::IEngineCore* engine) {
    delete engine;
}
