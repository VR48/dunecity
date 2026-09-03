#!/usr/bin/env bash
# Reproducible Emscripten browser build for DuneCity.
#
# Output (under build/emscripten/bin/ by default):
#   dunecity.html
#   dunecity.js
#   dunecity.wasm
#   dunecity.data   (preloaded game assets)
#
# Requires: git, cmake, python3.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EMSDK_VERSION="$(tr -d '[:space:]' < "${ROOT}/tools/web/emsdk-version.txt")"
EMSDK_DIR="${EMSDK_DIR:-${ROOT}/.emsdk}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build/emscripten}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-2}"

echo "==> DuneCity Emscripten build"
echo "    repo:        ${ROOT}"
echo "    emsdk:       ${EMSDK_DIR}"
echo "    emsdk ver:   ${EMSDK_VERSION}"
echo "    build dir:   ${BUILD_DIR}"
echo "    build type:  ${BUILD_TYPE}"

if [[ ! -d "${EMSDK_DIR}/.git" ]]; then
    echo "==> Cloning emsdk into ${EMSDK_DIR}"
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "${EMSDK_DIR}"
fi

pushd "${EMSDK_DIR}" >/dev/null
if ! ./emsdk list | grep -q "${EMSDK_VERSION}"; then
    echo "==> Installing Emscripten ${EMSDK_VERSION}"
    ./emsdk install "${EMSDK_VERSION}"
fi
./emsdk activate "${EMSDK_VERSION}"
# shellcheck disable=SC1091
source ./emsdk_env.sh
popd >/dev/null

command -v emcc >/dev/null
echo "==> Using $(emcc --version | head -1)"

echo "==> Prebuilding Emscripten SDL ports (serial cache warmup)"
unset EM_CACHE_IS_LOCKED
embuilder build sdl2 sdl2_mixer sdl2_ttf

rm -rf "${BUILD_DIR}"
emcmake cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DDUNECITY_BUILD_TESTS=OFF \
    -DDUNECITY_ENABLE_PCH=OFF

cmake --build "${BUILD_DIR}" --target dunecity -j "${JOBS}"

OUT_DIR="${BUILD_DIR}/bin"
HTML="${OUT_DIR}/dunecity.html"
JS="${OUT_DIR}/dunecity.js"
WASM="${OUT_DIR}/dunecity.wasm"

for artifact in "${HTML}" "${JS}" "${WASM}"; do
    if [[ ! -s "${artifact}" ]]; then
        echo "ERROR: expected non-empty artifact missing: ${artifact}" >&2
        exit 1
    fi
done

echo ""
echo "==> Build succeeded"
echo "    ${HTML}  $(stat -c%s "${HTML}") bytes"
echo "    ${JS}    $(stat -c%s "${JS}") bytes"
echo "    ${WASM}  $(stat -c%s "${WASM}") bytes"
if [[ -f "${OUT_DIR}/dunecity.data" ]]; then
    echo "    ${OUT_DIR}/dunecity.data  $(stat -c%s "${OUT_DIR}/dunecity.data") bytes"
fi
echo ""
echo "Serve locally, e.g.:"
echo "  cd ${OUT_DIR} && python3 -m http.server 8080"
echo "  open http://127.0.0.1:8080/dunecity.html"
