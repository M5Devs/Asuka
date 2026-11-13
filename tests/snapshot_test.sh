#!/usr/bin/env bash
#
# Regression test for the visual snapshot system (IEngineCore::save_snapshot /
# load_snapshot) against the FM Towns engine.
#
# Checks three things:
#   1. save_snapshot() writes a loadable state file and a BMP thumbnail beside
#      it (the thumbnail has a valid 24-bit BMP header at the frame size);
#   2. load_snapshot() accepts that file;
#   3. the state round-trips deterministically - running N frames straight
#      produces the same final frame as loading a state saved at frame N-1 and
#      running one more frame.
#
# Needs the FM Towns BIOS and a media image; if either is missing the test
# skips (exit 0) rather than failing, so it stays usable on a bare checkout.
#
# Usage: tests/snapshot_test.sh <runner> <fmtowns_plugin> <bios_dir> <media>

set -eu

runner="$1"
plugin="$2"
bios="$3"
media="$4"

if [[ ! -f "$media" || ! -d "$bios" ]]; then
    echo "SKIP: FM Towns BIOS or media not present"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

run() { "$runner" --plugin "$plugin" --bios "$bios" --media "$media" "$@"; }

# 1. Save a state at frame 120 and confirm both artefacts appear.
run --frames 120 --save-snapshot "$tmp/slot1.state" >"$tmp/save.log" 2>&1 || {
    echo "FAIL: save run exited non-zero" >&2
    cat "$tmp/save.log" >&2
    exit 1
}
[[ -s "$tmp/slot1.state" ]] || { echo "FAIL: no state file written" >&2; exit 1; }
[[ -s "$tmp/slot1.bmp" ]]   || { echo "FAIL: no thumbnail written" >&2; exit 1; }

# 2. Load the state and run one frame.
run --frames 1 --load-snapshot "$tmp/slot1.state" >"$tmp/load.log" 2>&1 || {
    echo "FAIL: load run exited non-zero" >&2
    cat "$tmp/load.log" >&2
    exit 1
}
grep -q "Teardown complete" "$tmp/load.log" || {
    echo "FAIL: no clean teardown after load" >&2
    exit 1
}

# 3. Determinism: 121 frames straight == state@120 + 1 frame.
run --frames 121 --dump-frame "$tmp/straight.ppm" >/dev/null 2>&1
run --frames 1 --load-snapshot "$tmp/slot1.state" \
    --dump-frame "$tmp/resumed.ppm" >/dev/null 2>&1
cmp -s "$tmp/straight.ppm" "$tmp/resumed.ppm" || {
    echo "FAIL: resumed frame differs from straight-run frame" >&2
    exit 1
}

python3 - "$tmp/slot1.bmp" <<'PY'
import struct
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()

if data[:2] != b"BM":
    sys.exit("FAIL: thumbnail is not a BMP")
(width, height) = struct.unpack_from("<ii", data, 18)
bpp = struct.unpack_from("<H", data, 28)[0]
if bpp != 24:
    sys.exit(f"FAIL: expected 24-bit BMP, got {bpp}-bit")
expected = 54 + ((width * 3 + 3) & ~3) * abs(height)
if len(data) != expected:
    sys.exit(f"FAIL: BMP size {len(data)} != expected {expected} "
             f"for {width}x{abs(height)}")
print(f"PASS: snapshot round-trip, {width}x{abs(height)} 24-bit thumbnail")
PY
