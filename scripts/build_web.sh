#!/usr/bin/env bash
#
# Build the Asuka WebAssembly bundle with Emscripten and stage it in web/dist/.
#
# Usage:
#   scripts/build_web.sh [--release] [--jobs N] [--out DIR]
#
# Requires the Emscripten SDK. Either have emcc/emcmake on PATH (e.g. after
# `source /path/to/emsdk/emsdk_env.sh`) or set EMSDK to an emsdk checkout and
# this script will source its environment.
#
# The engine (Geartowns) is linked into a single WASM module - no dlopen - and
# the artefacts land in web/dist/ ready for Cloudflare Pages.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
BUILD_DIR="${ROOT_DIR}/build-web"
OUT_DIR="${ROOT_DIR}/web/dist"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs) JOBS="$2"; shift 2 ;;
        --out)  OUT_DIR="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,15p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# --- Locate Emscripten -------------------------------------------------------
if ! command -v emcmake >/dev/null 2>&1; then
    for candidate in "${EMSDK:-}" /emsdk "$HOME/emsdk" /opt/emsdk /tmp/emsdk; do
        if [[ -n "${candidate}" && -f "${candidate}/emsdk_env.sh" ]]; then
            # shellcheck disable=SC1091
            source "${candidate}/emsdk_env.sh" >/dev/null 2>&1 || true
            break
        fi
    done
fi

if ! command -v emcmake >/dev/null 2>&1; then
    echo "error: Emscripten not found." >&2
    echo "  Install it, then re-run:" >&2
    echo "    git clone https://github.com/emscripten-core/emsdk.git ~/emsdk" >&2
    echo "    cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest" >&2
    echo "    source ~/emsdk/emsdk_env.sh" >&2
    exit 1
fi

if [[ ! -f "${ROOT_DIR}/plugins/fmtowns/vendor/Geartowns/src/geartowns_core.cpp" ]]; then
    echo "==> Fetching the Geartowns submodule"
    git -C "${ROOT_DIR}" submodule update --init --recursive
fi

echo "==> Emscripten: $(emcc --version | head -1)"
echo "==> Configuring (emcmake cmake) in ${BUILD_DIR}"
emcmake cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DASUKA_BUILD_WEB=ON \
    -DASUKA_BUILD_SDL=OFF

echo "==> Building (jobs=${JOBS})"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

# --- Stage the deployable bundle --------------------------------------------
echo "==> Staging into ${OUT_DIR}"
mkdir -p "${OUT_DIR}"
cp -f "${ROOT_DIR}/web/index.html" "${OUT_DIR}/index.html"
cp -f "${ROOT_DIR}/web/_headers"   "${OUT_DIR}/_headers"

# Emscripten emits asuka_web.js (+ .wasm and a worker); copy whatever is there.
shopt -s nullglob
for artefact in "${BUILD_DIR}/web/"asuka_web*; do
    cp -f "${artefact}" "${OUT_DIR}/"
done

if [[ ! -f "${OUT_DIR}/asuka_web.wasm" ]]; then
    echo "error: asuka_web.wasm was not produced" >&2
    exit 1
fi

# Sanity-check the bundle: the WASM magic number, and the shell wiring.
if [[ "$(head -c4 "${OUT_DIR}/asuka_web.wasm" | od -An -tx1 | tr -d ' \n')" != "0061736d" ]]; then
    echo "error: asuka_web.wasm is not a WebAssembly module" >&2
    exit 1
fi
grep -q "asuka_web.js" "${OUT_DIR}/index.html" || {
    echo "error: index.html does not load asuka_web.js" >&2; exit 1; }
grep -q "Cross-Origin-Embedder-Policy" "${OUT_DIR}/_headers" || {
    echo "warning: _headers is missing the COOP/COEP isolation headers" >&2; }

echo "==> Done. Bundle contents:"
ls -lh "${OUT_DIR}"
echo
echo "Test locally:   python3 scripts/serve_web.py ${OUT_DIR}"
