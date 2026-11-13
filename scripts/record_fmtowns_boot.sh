#!/usr/bin/env bash
#
# Record the FM Towns After Burner boot and encode it to MP4 + GIF.
#
# The emulator is run for a fixed number of frames with every frame dumped as a
# PPM, the frames are normalised onto one canvas (the machine changes video mode
# mid-boot), a dead stretch is trimmed, and the result is encoded.
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

echo "== Recording $frames frames =="
rm -rf "$raw" "$seq" "$norm"
mkdir -p "$raw"
"$runner" --plugin "$plugin" --bios "$bios_dir" --media "$cue" \
    --frames "$frames" --dump-frames "$raw" > /tmp/asuka_record.log 2>&1 || {
        tail -20 /tmp/asuka_record.log >&2
        fail "runner exited non-zero"
    }
echo "  dumped $(find "$raw" -name '*.ppm' | wc -l) frames"

# Build the demo sequence. The machine sits on a byte-identical boot screen for
# a long stretch, so that dead time is cut: the first block keeps the POST and
# the boot screen appearing, then playback jumps straight to the end of the
# static stretch. Because the frames at the cut are identical, the join is
# invisible. The second block keeps the mode switch, the title and the fade.
echo "== Building demo sequence =="
python3 - "$raw" "$seq" <<'PY'
import os, shutil, sys

raw, seq = sys.argv[1], sys.argv[2]
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
PY

echo "== Normalising to a single canvas =="
python3 scripts/normalise_frames.py "$seq" "$norm"

echo "== Encoding MP4 =="
ffmpeg -y -v error -framerate 60 -i "$norm/frame_%04d.ppm" \
    -c:v libx264 -pix_fmt yuv420p -preset slow -crf 18 -movflags +faststart \
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
