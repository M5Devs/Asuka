#!/usr/bin/env bash
#
# End-to-end FM Towns boot verification.
#
# Boots a real disc image (After Burner) through the asuka_runner, dumps a
# frame after the game has switched to its graphics mode, and asserts that the
# picture contains active, varied video rather than a blank or uniform screen.
#
# Assets are NOT tracked: put the BIOS ROMs in test_assets/bios/ and the disc
# image in test_assets/media/ (see scripts/fetch_test_assets.sh). Both live
# under the git-ignored test_assets/ tree.
#
# Usage: scripts/verify_fmtowns_boot.sh [frames]

set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

bios_dir="${ASUKA_TEST_BIOS:-test_assets/bios}"
media_dir="${ASUKA_TEST_MEDIA:-test_assets/media}"
frames="${1:-1500}"
out="${ASUKA_TEST_FRAME:-test_assets/afterburner_frame.ppm}"

runner="build/bin/asuka_runner"
plugin="build/plugins/fmtowns/libasuka_fmtowns.so"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$runner" ] || fail "runner not built: $runner"
[ -f "$plugin" ] || fail "plugin not built: $plugin"
[ -d "$bios_dir" ] || fail "missing BIOS dir: $bios_dir"

cue="$(find "$media_dir" -iname '*.cue' -print -quit 2>/dev/null)"
[ -n "$cue" ] || fail "no .cue disc image under $media_dir"

echo "== Asuka FM Towns boot verification =="
echo "  plugin : $plugin"
echo "  bios   : $bios_dir"
echo "  media  : $cue"
echo "  frames : $frames"

"$runner" --plugin "$plugin" --bios "$bios_dir" --media "$cue" \
    --frames "$frames" --dump-frame "$out" >/tmp/asuka_boot.log 2>&1 || {
        tail -20 /tmp/asuka_boot.log >&2
        fail "runner exited non-zero"
    }

grep -q "Teardown complete" /tmp/asuka_boot.log || fail "no clean teardown"
[ -f "$out" ] || fail "no frame dumped at $out"

# Assert the frame has content: enough distinct colours and enough non-black
# coverage. The FM Towns graphics mode shows a rendered title, not a flat field.
python3 - "$out" <<'PY' || exit 1
import sys
path = sys.argv[1]
with open(path, "rb") as fh:
    assert fh.readline().strip() == b"P6", "not a binary PPM"
    w, h = map(int, fh.readline().split())
    fh.readline()
    data = fh.read()

pixels = w * h
colors = len({data[i:i + 3] for i in range(0, pixels * 3, 3)})
nonblack = sum(1 for i in range(0, pixels * 3, 3) if data[i:i + 3] != b"\x00\x00\x00")

print(f"  frame  : {w}x{h}")
print(f"  colors : {colors} distinct")
print(f"  ink    : {100 * nonblack // pixels}% of pixels are non-black")

if colors < 32:
    print("FAIL: frame is nearly flat (no active video)", file=sys.stderr)
    sys.exit(1)
if nonblack * 100 // pixels < 10:
    print("FAIL: frame is almost entirely black", file=sys.stderr)
    sys.exit(1)
print("PASS: frame contains active, varied video")
PY

# Assert host input actually reaches the machine. After Burner polls Game Port 1
# (the Towns Pad) rather than the keyboard, so the runner's --press must be
# mirrored there or the title screen ignores every control. Boot twice from the
# same frame count, once idle and once holding Start/A near the end, and require
# the two final frames to differ: identical frames mean the input was dropped.
echo
echo "== Input routing check (Game Port 1 / Towns Pad) =="
idle="$out.idle.ppm"
pressed="$out.pressed.ppm"
press_frames=$((frames + 1200))

"$runner" --plugin "$plugin" --bios "$bios_dir" --media "$cue" \
    --frames "$press_frames" --dump-frame "$idle" >/tmp/asuka_input_idle.log 2>&1 \
    || fail "idle run exited non-zero"
"$runner" --plugin "$plugin" --bios "$bios_dir" --media "$cue" \
    --frames "$press_frames" \
    --press $((press_frames - 100)):start \
    --press $((press_frames - 80)):a \
    --press $((press_frames - 60)):start \
    --dump-frame "$pressed" >/tmp/asuka_input_press.log 2>&1 \
    || fail "pressed run exited non-zero"

python3 - "$idle" "$pressed" <<'PY' || exit 1
import sys

def load(path):
    with open(path, "rb") as fh:
        assert fh.readline().strip() == b"P6", "not a binary PPM"
        w, h = map(int, fh.readline().split())
        fh.readline()
        return w, h, fh.read()

w, h, idle = load(sys.argv[1])
w2, h2, pressed = load(sys.argv[2])
assert (w, h) == (w2, h2), "frame size changed between runs"

changed = sum(1 for i in range(0, w * h * 3, 3) if idle[i:i + 3] != pressed[i:i + 3])
pct = 100 * changed // (w * h)
print(f"  changed: {changed} px ({pct}% of the frame) after Start/A presses")

if changed == 0:
    print("FAIL: input had no effect - Game Port 1 routing is broken", file=sys.stderr)
    sys.exit(1)
print("PASS: host input reaches the machine")
PY

rm -f "$idle" "$pressed"
