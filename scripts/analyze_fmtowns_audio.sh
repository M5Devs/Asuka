#!/usr/bin/env bash
#
# Capture and analyse FM Towns audio from the emulator.
#
# Runs the FM Towns plugin for a fixed number of frames with the runner's
# --dump-audio option, then prints a per-second loudness and tonality map of the
# captured WAV.
#
# Why this exists: the After Burner boot and title screens are silent. This
# script records the audio path end to end and shows where sound actually
# appears, so the silence can be told apart from a broken mixer:
#   * the boot and title are silent because the guest never issues a CD-DA play
#     command and does not program the FM chip until later;
#   * the attract mode that follows does produce FM music and sound effects.
# The map marks loud seconds and, separately, seconds whose spectrum is
# harmonic (music) rather than noise (sound effects).
#
# Assets are not tracked: put the BIOS ROMs in test_assets/bios/ and the disc
# image in test_assets/media/ (see scripts/fetch_test_assets.sh).
#
# Usage: scripts/analyze_fmtowns_audio.sh [frames]

set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

frames="${1:-4200}"
bios_dir="${ASUKA_TEST_BIOS:-test_assets/bios}"
media_dir="${ASUKA_TEST_MEDIA:-test_assets/media}"
wav="${ASUKA_TEST_AUDIO:-test_assets/fmtowns_audio.wav}"

runner="build/bin/asuka_runner"
plugin="build/plugins/fmtowns/libasuka_fmtowns.so"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$runner" ] || fail "runner not built: $runner"
[ -f "$plugin" ] || fail "plugin not built: $plugin"
[ -d "$bios_dir" ] || fail "missing BIOS dir: $bios_dir (run scripts/fetch_test_assets.sh)"
cue="$(find "$media_dir" -iname '*.cue' -print -quit 2>/dev/null)"
[ -n "$cue" ] || fail "no .cue disc image under $media_dir (run scripts/fetch_test_assets.sh)"

echo "== Capturing $frames frames of audio =="
mkdir -p "$(dirname "$wav")"
"$runner" --plugin "$plugin" --bios "$bios_dir" --media "$cue" \
    --frames "$frames" --dump-audio "$wav" >/tmp/asuka_audio.log 2>&1 || {
        tail -20 /tmp/asuka_audio.log >&2
        fail "runner exited non-zero"
    }
grep -q "Teardown complete" /tmp/asuka_audio.log || fail "no clean teardown"
[ -f "$wav" ] || fail "no WAV written to $wav"
grep -E "wrote audio to" /tmp/asuka_audio.log | sed 's/^/  /'

echo "== Analysing $wav =="
python3 - "$wav" <<'PY' || exit 1
import sys
import wave

import numpy as np

path = sys.argv[1]

with wave.open(path, "rb") as w:
    channels = w.getnchannels()
    width = w.getsampwidth()
    rate = w.getframerate()
    pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)

if channels != 2 or width != 2:
    print(f"FAIL: expected 16-bit stereo, got {width * 8}-bit {channels}ch",
          file=sys.stderr)
    sys.exit(1)

audio = pcm.reshape(-1, 2).astype(np.float32)
seconds = len(audio) // rate
print(f"  format : {width * 8}-bit {channels}ch {rate} Hz, {len(audio) / rate:.1f} s")
print(f"  peak   : {int(np.abs(audio).max())}")

# Per-second loudness and tonality. A musical note has many strong spectral
# peaks (its harmonics); broadband noise has few. Both are loud, so the two
# measures together separate music from sound effects.
map_loud = []
map_music = []
for s in range(seconds):
    chunk = audio[s * rate:(s + 1) * rate, 0]
    rms = float(np.sqrt((chunk ** 2).mean()))
    mono = chunk - chunk.mean()
    spectrum = np.abs(np.fft.rfft(mono * np.hanning(len(mono))))
    peaks = int(((spectrum[1:-1] > spectrum[:-2])
                 & (spectrum[1:-1] > spectrum[2:])
                 & (spectrum[1:-1] > spectrum.mean() * 4)).sum())
    map_loud.append("X" if rms > 100 else ("-" if rms > 1 else "."))
    map_music.append("M" if peaks > 50 else " ")

print()
print("  loudness  (X loud, - quiet, . silent):")
for start in range(0, seconds, 60):
    print(f"    {start:4d}s |{''.join(map_loud[start:start + 60])}|")
print("  tonality  (M = harmonic, likely music):")
for start in range(0, seconds, 60):
    print(f"    {start:4d}s |{''.join(map_music[start:start + 60])}|")

loud = [i for i, c in enumerate(map_loud) if c == "X"]
music = [i for i, c in enumerate(map_music) if c == "M"]
print()
if not loud:
    print("FAIL: no audible audio in the whole capture (audio path broken?)",
          file=sys.stderr)
    sys.exit(1)
print(f"  audible : {len(loud)} s, first at {loud[0]}s, last at {loud[-1]}s")
print(f"  musical : {len(music)} s"
      + (f", first at {music[0]}s, last at {music[-1]}s" if music else ""))
if music:
    print("PASS: audio path works; the guest emits FM music in attract mode")
else:
    print("PASS: audio path works, but the capture is all sound effects")
PY
