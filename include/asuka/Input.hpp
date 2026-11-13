#pragma once

#include <cstdint>

namespace asuka {

// Host-level input keys, the identifiers the runner passes to
// IEngineCore::send_input().
//
// The ABI deliberately keeps `key_code` a plain integer so a core can define
// its own scheme, but a generic host (the SDL runner) cannot know each core's
// native codes. This shared, deliberately small set is the contract: the host
// translates physical input (SDL scancodes, gamepad buttons) into these keys,
// and each core maps them to its own. It is the object-oriented analogue of
// Libretro's RETRO_DEVICE_ID_JOYPAD_* identifiers.
//
// A core that has no use for a key simply ignores it.
enum class Key : uint32_t {
    None = 0,

    // Directional pad.
    Up,
    Down,
    Left,
    Right,

    // System buttons.
    Start,
    Select,

    // Action buttons, most-common first.
    A,
    B,
    X,
    Y,
    L,
    R,
};

}  // namespace asuka
