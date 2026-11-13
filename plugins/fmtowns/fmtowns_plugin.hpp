#pragma once

#include "asuka/IEngineCore.hpp"

// Factory entry points exported by the FM Towns plugin. The desktop runners
// resolve these by name through PluginLoader (dlsym/GetProcAddress); the web
// build links the plugin statically and calls them directly.
extern "C" {

asuka::IEngineCore* create_engine();
void destroy_engine(asuka::IEngineCore* engine);

}  // extern "C"
