#!/usr/bin/env bash
#
# Regression test for the PC-98 (NP2kai) engine plugin.
#
# The PC-98 core boots from its built-in BIOS I/O emulation, so - unlike the FM
# Towns test - this one needs no copyrighted BIOS or media image and always
# runs. It checks:
#   1. the plugin loads and the machine boots: frames render at 640x400 XRGB8888
#      and the OPNA/FM audio path produces samples;
#   2. save_snapshot() writes a state file plus a valid 640x400 BMP thumbnail;
#   3. the state round-trips deterministically - running N frames straight gives
#      the same final frame as loading a state saved earlier and running on;
#   4. raw PC-98 scancodes fed through send_scancode() are accepted without
#      disturbing the run (the virtual keyboard's input path).
#
# Usage: tests/pc98_test.sh <runner> <pc98_plugin>

set -eu

runner="$1"
plugin="$2"

if [[ ! -f "$plugin" ]]; then
    echo "SKIP: PC-98 plugin not built ($plugin)"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

run() { "$runner" --plugin "$plugin" "$@"; }

# 1. Boot and render: expect 640x400 output and a non-empty audio buffer.
run --frames 30 --dump-frame "$tmp/boot.ppm" >"$tmp/boot.log" 2>&1 || {
    echo "FAIL: boot run exited non-zero" >&2
    cat "$tmp/boot.log" >&2
    exit 1
}
grep -q "640x400" "$tmp/boot.log" || {
    echo "FAIL: engine did not report a 640x400 framebuffer" >&2
    cat "$tmp/boot.log" >&2
    exit 1
}
grep -q "audio buffer    : present" "$tmp/boot.log" || {
    echo "FAIL: no audio samples produced" >&2
    exit 1
}
# PPM header: "P6\n<w> <h>\n255\n".
head -c 2 "$tmp/boot.ppm" | grep -q "P6" || {
    echo "FAIL: frame dump is not a P6 PPM" >&2
    exit 1
}

# 2. Save a state at frame 30 and confirm both artefacts appear. The thumbnail
#    is a bottom-up 24-bit BMP: 54-byte header + 640*400*3 bytes.
run --frames 30 --save-snapshot "$tmp/slot1.state" >"$tmp/save.log" 2>&1 || {
    echo "FAIL: save run exited non-zero" >&2
    cat "$tmp/save.log" >&2
    exit 1
}
[[ -s "$tmp/slot1.state" ]] || { echo "FAIL: no state file written" >&2; exit 1; }
[[ -s "$tmp/slot1.bmp" ]]   || { echo "FAIL: no thumbnail written" >&2; exit 1; }

expected_bmp=$((54 + 640 * 400 * 3))
actual_bmp=$(stat -c %s "$tmp/slot1.bmp")
[[ "$actual_bmp" == "$expected_bmp" ]] || {
    echo "FAIL: thumbnail is $actual_bmp bytes, expected $expected_bmp" >&2
    exit 1
}
[[ "$(head -c 2 "$tmp/slot1.bmp")" == "BM" ]] || {
    echo "FAIL: thumbnail is not a BMP" >&2
    exit 1
}

# 3. Determinism: 60 frames straight == state@30 + 30 frames.
run --frames 60 --dump-frame "$tmp/straight.ppm" >/dev/null 2>&1
run --frames 30 --load-snapshot "$tmp/slot1.state" \
    --dump-frame "$tmp/resumed.ppm" >/dev/null 2>&1
cmp -s "$tmp/straight.ppm" "$tmp/resumed.ppm" || {
    echo "FAIL: state did not round-trip deterministically" >&2
    exit 1
}

# 4. Scancode input path: HELP (0x3f), STOP (0x60) and 'A' (0x1d) are pressed
#    mid-run; the machine must keep rendering cleanly.
run --frames 30 --press-scancode 5:0x3f --press-scancode 10:0x60 \
    --press-scancode 15:0x1d --dump-frame "$tmp/keys.ppm" >"$tmp/keys.log" 2>&1 || {
    echo "FAIL: scancode-injection run exited non-zero" >&2
    cat "$tmp/keys.log" >&2
    exit 1
}
grep -q "640x400" "$tmp/keys.log" || {
    echo "FAIL: engine stopped rendering after scancode input" >&2
    exit 1
}

echo "PASS: PC-98 engine boots, renders, snapshots and accepts scancodes"
