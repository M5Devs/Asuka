#!/usr/bin/env bash
#
# Regression test for the Sharp X68000 (PX68k) engine plugin.
#
# PX68k needs the X68000 IPL/character ROMs to power on, so this test skips
# (exit 0) when the BIOS directory is missing or empty, exactly as the FM Towns
# snapshot test does. When the ROMs are present it checks:
#   1. the plugin loads and the machine boots: frames render at 768x512
#      XRGB8888 and the YM2151/ADPCM audio path produces samples;
#   2. save_snapshot() writes a state file plus a valid 768x512 BMP thumbnail;
#   3. the state round-trips deterministically - running N frames straight gives
#      the same final frame as loading a state saved earlier and running on.
#
# Usage: tests/x68k_test.sh <runner> <x68k_plugin> <bios_dir>

set -eu

runner="$1"
plugin="$2"
bios="$3"

if [[ ! -f "$plugin" ]]; then
    echo "SKIP: X68000 plugin not built ($plugin)"
    exit 0
fi

# The core needs the IPL ROM to boot; without it the test would only exercise
# the error path, so skip on a bare checkout.
if [[ ! -f "$bios/iplrom.dat" ]]; then
    echo "SKIP: X68000 BIOS not present ($bios/iplrom.dat)"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

run() { "$runner" --plugin "$plugin" --bios "$bios" "$@"; }

# 1. Boot and render: expect 768x512 output and a non-empty audio buffer.
run --frames 60 --dump-frame "$tmp/boot.ppm" >"$tmp/boot.log" 2>&1 || {
    echo "FAIL: boot run exited non-zero" >&2
    cat "$tmp/boot.log" >&2
    exit 1
}
grep -q "768x512" "$tmp/boot.log" || {
    echo "FAIL: engine did not report a 768x512 framebuffer" >&2
    cat "$tmp/boot.log" >&2
    exit 1
}
grep -q "audio buffer    : present" "$tmp/boot.log" || {
    echo "FAIL: no audio samples produced" >&2
    exit 1
}
grep -q "Teardown complete" "$tmp/boot.log" || {
    echo "FAIL: engine did not tear down cleanly" >&2
    exit 1
}
# PPM header: "P6\n<w> <h>\n255\n".
head -c 2 "$tmp/boot.ppm" | grep -q "P6" || {
    echo "FAIL: frame dump is not a P6 PPM" >&2
    exit 1
}

# 2. Save a state and confirm both artefacts appear. The thumbnail is a
#    bottom-up 24-bit BMP: 54-byte header + 768*512*3 bytes.
run --frames 60 --save-snapshot "$tmp/slot1.state" >"$tmp/save.log" 2>&1 || {
    echo "FAIL: save run exited non-zero" >&2
    cat "$tmp/save.log" >&2
    exit 1
}
[[ -s "$tmp/slot1.state" ]] || { echo "FAIL: no state file written" >&2; exit 1; }
[[ -s "$tmp/slot1.bmp" ]]   || { echo "FAIL: no thumbnail written" >&2; exit 1; }

expected_bmp=$((54 + 768 * 512 * 3))
actual_bmp=$(stat -c %s "$tmp/slot1.bmp")
[[ "$actual_bmp" == "$expected_bmp" ]] || {
    echo "FAIL: thumbnail is $actual_bmp bytes, expected $expected_bmp" >&2
    exit 1
}
[[ "$(head -c 2 "$tmp/slot1.bmp")" == "BM" ]] || {
    echo "FAIL: thumbnail is not a BMP" >&2
    exit 1
}

# 3. Determinism: 90 frames straight == state@60 + 30 frames.
run --frames 90 --dump-frame "$tmp/straight.ppm" >/dev/null 2>&1
run --frames 30 --load-snapshot "$tmp/slot1.state" \
    --dump-frame "$tmp/resumed.ppm" >/dev/null 2>&1
cmp -s "$tmp/straight.ppm" "$tmp/resumed.ppm" || {
    echo "FAIL: state did not round-trip deterministically" >&2
    exit 1
}

echo "PASS: X68000 engine boots, renders, produces audio and snapshots cleanly"
