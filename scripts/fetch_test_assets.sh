#!/usr/bin/env bash
#
# Fetch the FM Towns test assets needed by scripts/verify_fmtowns_boot.sh.
#
#   * BIOS ROM set, from Abdess/retrobios (bios/Fujitsu/FM Towns).
#   * After Burner (1987, Sega) disc image, from the Internet Archive
#     fmtownscdcollection item (CUE + BIN inside a ZIP).
#
# Everything lands in the git-ignored test_assets/ tree. Both the ROMs and the
# disc are copyrighted; this is a local convenience for testing only.
#
# Usage: scripts/fetch_test_assets.sh

set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

bios_dir="test_assets/bios"
media_dir="test_assets/media"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$bios_dir" "$media_dir"

echo "== Fetching FM Towns BIOS (retrobios) =="
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/Abdess/retrobios.git "$work/retrobios"
git -C "$work/retrobios" sparse-checkout set --no-cone '/bios/Fujitsu/FM Towns/*'
cp "$work/retrobios/bios/Fujitsu/FM Towns/"*.ROM "$bios_dir/"
echo "  copied $(find "$bios_dir" -name '*.ROM' | wc -l) ROM files"

echo "== Fetching After Burner disc (archive.org) =="
zip_url="https://archive.org/download/fmtownscdcollection/FM-Towns%20CD/Afterburner%20%281987%29%28Sega%29%28Jp-En%29.zip"
curl -L -o "$work/afterburner.zip" "$zip_url"
if command -v 7z >/dev/null 2>&1; then
    7z x -y -o"$media_dir" "$work/afterburner.zip" >/dev/null
else
    python3 - "$work/afterburner.zip" "$media_dir" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    z.extractall(sys.argv[2])
PY
fi
echo "  extracted $(find "$media_dir" -type f | wc -l) files"

echo "Done. Now run: scripts/verify_fmtowns_boot.sh"
