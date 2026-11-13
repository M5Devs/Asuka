#!/usr/bin/env bash
#
# Record the FM Towns After Burner boot and encode it to MP4 + GIF.
#
# The emulator is run for a fixed number of frames with every frame dumped as a
# PPM and the audio captured to a WAV, the frames are normalised onto one canvas
# (the machine changes video mode mid-boot), a dead stretch is trimmed, and the
# result is encoded. The audio is spliced to match the same trim and muxed into
# the MP4.
#
# The boot and title are silent (the guest never issues a CD-DA play and does
# not unmute the FM chip until attract mode), so the muxed audio track is valid
# but effectively silent. scripts/analyze_fmtowns_audio.sh shows where sound
# actually appears.
#
# Assets are not tracked: run scripts/fetch_test_assets.sh first. Everything
# below lives under the git-ignored test_assets/ tree.
#
# Usage: scripts/record_fmtowns_boot.sh [frames] [--keep-frames]

set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

bios_dir="${ASUKA_TEST_BIOS:-test_assets/bios}"
media_dir="${ASUKA_TEST_MEDIA:-test_assets/media}"
outdir="test_assets"

frames="${1:-1800}"
keep_frames=false
[ "${2:-}" = "--keep-frames" ] && keep_frames=true

runner="build/bin/asuka_runner"
plugin="build/plugins/fmtowns/libasuka_fmtowns.so"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$runner" ] || fail "runner not built: $runner"
[ -f "$plugin" ] || fail "plugin not built: $plugin"
[ -d "$bios_dir" ] || fail "missing BIOS dir: $bios_dir (run scripts/fetch_test_assets.sh)"
command -v ffmpeg >/dev/null 2>&1 || fail "ffmpeg not found (needed for encoding)"
cue="$(find "$media_dir" -iname '*.cue' -print -quit 2>/dev/null)"
[ -n "$cue" ] || fail "no .cue disc image under $media_dir (run scripts/fetch_test_assets.sh)"

raw="$outdir/frames"
seq="$outdir/frames_seq"
norm="$outdir/frames_norm"
wav="$outdir/afterburner_boot.wav"
wav_map="$outdir/afterburner_boot.audio_frames"
wav_seq="$outdir/afterburner_boot_seq.wav"

echo "== Recording $frames frames (video + audio) =="
rm -rf "$raw" "$seq" "$norm"
rm -f "$wav" "$wav_map" "$wav_seq"
mkdir -p "$raw"
"$runner" --plugin "$plugin" --bios "$bios_dir" --media "$cue" \
    --frames "$frames" --dump-frames "$raw" \
    --dump-audio "$wav" --dump-audio-frames "$wav_map" > /tmp/asuka_record.log 2>&1 || {
        tail -20 /tmp/asuka_record.log >&2
        fail "runner exited non-zero"
    }
echo "  dumped $(find "$raw" -name '*.ppm' | wc -l) frames"
grep -E "wrote audio to" /tmp/asuka_record.log | sed 's/^/  /'

# Build the demo sequence. The machine sits on a byte-identical boot screen for
# a long stretch, so that dead time is cut: the first block keeps the POST and
# the boot screen appearing, then playback jumps straight to the end of the
# static stretch. Because the frames at the cut are identical, the join is
# invisible. The second block keeps the mode switch, the title and the fade.
# The same plan drives the audio splice, so sound stays in sync.
echo "== Building demo sequence =="
python3 - "$raw" "$seq" "$wav" "$wav_map" "$wav_seq" <<'PY'
import os
import shutil
import sys
import wave

raw, seq, wav, wav_map, wav_seq = sys.argv[1:6]
# (first, last) inclusive, 0-based, from the raw recording. The join is between
# raw frame 200 and raw frame 1100, which are byte-identical (verified), so the
# cut is invisible. 201 + 700 frames = 901 frames = 15.0s at 60fps.
plan = [(0, 200), (1100, 1799)]

os.makedirs(seq, exist_ok=True)
out = 0
for first, last in plan:
    for n in range(first, last + 1):
        src = os.path.join(raw, "frame_%04d.ppm" % n)
        if not os.path.exists(src):
            sys.exit("missing %s (recorded fewer frames?)" % src)
        shutil.copyfile(src, os.path.join(seq, "frame_%04d.ppm" % out))
        out += 1
print("  selected %d frames" % out)

# Splice the audio to the same frame plan. The engine emits a variable number of
# samples per frame, so the sidecar map (samples/frame) tells us how much of the
# WAV each selected frame owns.
with wave.open(wav, "rb") as w:
    channels, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
    pcm = w.readframes(w.getnframes())

counts = [int(line) for line in open(wav_map) if line.strip()]
frame_bytes = channels * width
# Prefix offsets so each selected frame's slice is O(1).
offsets = [0]
for c in counts:
    offsets.append(offsets[-1] + c * frame_bytes)
kept = []
for first, last in plan:
    for n in range(first, last + 1):
        if n >= len(counts):
            sys.exit("audio map shorter than frame plan")
        kept.append(pcm[offsets[n]:offsets[n + 1]])
data = b"".join(kept)

with wave.open(wav_seq, "wb") as w:
    w.setnchannels(channels)
    w.setsampwidth(width)
    w.setframerate(rate)
    w.writeframes(data)
print("  spliced audio: %d frames, %.1f s"
      % (len(data) // frame_bytes, len(data) / frame_bytes / rate))
PY

echo "== Normalising to a single canvas =="
python3 scripts/normalise_frames.py "$seq" "$norm"

echo "== Encoding MP4 =="
ffmpeg -y -v error -framerate 60 -i "$norm/frame_%04d.ppm" -i "$wav_seq" \
    -map 0:v -map 1:a -c:v libx264 -pix_fmt yuv420p -preset slow -crf 18 \
    -c:a aac -b:a 128k -shortest -movflags +faststart \
    "$outdir/afterburner_boot.mp4"
echo "  $(du -h "$outdir/afterburner_boot.mp4" | cut -f1)  $outdir/afterburner_boot.mp4"

echo "== Encoding GIF =="
ffmpeg -y -v error -framerate 60 -i "$norm/frame_%04d.ppm" \
    -vf "fps=20,split[a][b];[a]palettegen=max_colors=256:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle" \
    -loop 0 "$outdir/afterburner_boot.gif"
echo "  $(du -h "$outdir/afterburner_boot.gif" | cut -f1)  $outdir/afterburner_boot.gif"

if [ "$keep_frames" = false ]; then
    echo "== Cleaning up raw frames =="
    rm -rf "$raw" "$seq" "$norm"
fi

echo "Done."
