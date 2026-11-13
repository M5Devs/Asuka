#!/usr/bin/env bash
#
# Regression test for the runner's audio capture.
#
# Runs the dummy engine, which always produces a known number of samples per
# frame, and checks that --dump-audio writes a valid 16-bit stereo WAV whose
# length matches --dump-audio-frames, and that the captured audio is not silent.
#
# Usage: tests/audio_capture_test.sh <runner> <dummy_plugin>

set -eu

runner="$1"
plugin="$2"
frames=10

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$runner" "$plugin" --frames "$frames" \
    --dump-audio "$tmp/out.wav" --dump-audio-frames "$tmp/frames.txt" \
    >"$tmp/log.txt" 2>&1 || {
        echo "FAIL: runner exited non-zero" >&2
        cat "$tmp/log.txt" >&2
        exit 1
    }
grep -q "Teardown complete" "$tmp/log.txt" || {
    echo "FAIL: no clean teardown" >&2
    exit 1
}

python3 - "$tmp/out.wav" "$tmp/frames.txt" "$frames" <<'PY'
import sys
import wave

import numpy as np

wav_path, map_path, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])

with wave.open(wav_path, "rb") as w:
    channels, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
    pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)

if (channels, width, rate) != (2, 2, 44100):
    sys.exit(f"FAIL: expected 16-bit stereo 44100 Hz, got "
             f"{width * 8}-bit {channels}ch {rate} Hz")

counts = [int(line) for line in open(map_path) if line.strip()]
if len(counts) != frames:
    sys.exit(f"FAIL: sidecar has {len(counts)} lines, expected {frames}")
if sum(counts) != len(pcm) // channels:
    sys.exit(f"FAIL: sidecar sums to {sum(counts)}, WAV has "
             f"{len(pcm) // channels} frames")
if int(np.abs(pcm).max()) == 0:
    sys.exit("FAIL: captured audio is silent (dummy engine should emit a tone)")

print(f"PASS: {frames} frames, {len(pcm) // channels} samples/channel, "
      f"peak {int(np.abs(pcm).max())}")
PY
