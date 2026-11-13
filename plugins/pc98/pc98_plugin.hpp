#pragma once

#include "asuka/IEngineCore.hpp"

// Factory entry points for the PC-98 engine.
//
// The desktop runners resolve the generic `create_engine`/`destroy_engine`
// symbols by name through PluginLoader (dlsym/GetProcAddress), so the shared
// plugin exports those. The web build links several engines into one module,
// where the generic names would collide, so it uses the PC-98-specific names
// below instead. Both are always available.
extern "C" {

asuka::IEngineCore* create_pc98_engine();
void destroy_pc98_engine(asuka::IEngineCore* engine);

}  // extern "C"
