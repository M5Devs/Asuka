/* Minimal MIDI backend for the Asuka build of PX68k.
 *
 * PX68k can route the emulated MIDI board to an internal synthesizer: a
 * Roland SC-55 port (midi/sc55) or the bundled Munt MT-32/CM-32L emulator
 * (midi/mt32emu). Both are large and the Munt half spawns a synthesis worker
 * thread, which the web build does not want. The X68000's own sound hardware -
 * the YM2151 FM chip and the MSM6258 ADPCM channel - is emulated by fmgen and
 * is unaffected; only the optional external-MIDI-module path is dropped.
 *
 * This file supplies the px68k_midiemu_* entry points libretro.c calls, with
 * the backend permanently disabled. Keeping the same names means libretro.c is
 * untouched. MIDI state tracking (midi/midi_shadow.c) is still compiled, so
 * save states keep working. */

#include <stddef.h>
#include <stdint.h>

#include <libretro.h>

#include "midi/midiemu/midiemu_libretro.h"

void px68k_midiemu_set_log_callback(retro_log_printf_t callback)
{
    (void)callback;
}

void px68k_midiemu_configure(int emulation, const char *system_dir,
                             const char *content_dir)
{
    (void)emulation;
    (void)system_dir;
    (void)content_dir;
}

int px68k_midiemu_enabled(void)
{
    return 0;
}

void px68k_midiemu_midi_short(size_t packed_msg)
{
    (void)packed_msg;
}

void px68k_midiemu_midi_long(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
}

void px68k_midiemu_mix(int16_t *stereo, size_t frames)
{
    (void)stereo;
    (void)frames;
}

void px68k_midiemu_set_volume(int volume_percent)
{
    (void)volume_percent;
}

void px68k_midiemu_state_load_begin(void) {}

void px68k_midiemu_runahead_begin(void) {}

void px68k_midiemu_runahead_end(void) {}

void px68k_midiemu_runahead_suppress(void) {}

void px68k_midiemu_runahead_reset(void) {}

void px68k_midiemu_shutdown(void) {}
