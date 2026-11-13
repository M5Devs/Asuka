#include <new>

#include "DummyEngine.hpp"

#include "asuka/Export.hpp"

// Exported C entry points. The host resolves these by name after dlopen /
// LoadLibrary and uses them to construct and destroy engine instances.
ASUKA_PLUGIN_API asuka::IEngineCore* create_engine() {
    return new (std::nothrow) asuka::plugins::dummy::DummyEngine();
}

ASUKA_PLUGIN_API void destroy_engine(asuka::IEngineCore* engine) {
    delete engine;
}
