#pragma once

// Single force-include entry point for the X68000 core.
//
// Two things must be in scope before any PX68k header is parsed:
//
//   1. PX68k's own compiler.h, which provides the integer typedefs its headers
//      assume are already defined (C translation units only, matching its
//      build; the C++ units pull the types in through their own includes).
//   2. The symbol-isolation shim, which renames the core's copies of the
//      Libretro API / libretro-common / fmgen symbols so they do not collide
//      with the PC-98 core's in the single-module web build.
//
// CMake de-duplicates repeated `-include` flags, so both are pulled in from
// this one wrapper rather than as two separate force-include options.
#ifndef __cplusplus
#include "compiler.h"
#endif

#include "x68k_symbol_prefix.h"
