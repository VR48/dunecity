#!/bin/sh
#
# Builds and runs the standalone network wire harness.
#
#   tests/wasm/run-network-wire-harness.sh wasm     # emcc + node (wasm32, 32-bit size_t)
#   tests/wasm/run-network-wire-harness.sh native   # host compiler (LP64)
#
# The wasm run is the one that matters for the overflow checks: on wasm32 size_t is 32 bits,
# which is exactly the case the additive bounds checks used to get wrong. The native run is
# there so the same harness can be sanity checked without an Emscripten toolchain.
#
# Run from the repository root. Exits non-zero if any check fails.

set -eu

MODE="${1:-wasm}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${ROOT}/build/wasm-harness"

mkdir -p "${OUT}"

SOURCES="${ROOT}/tests/wasm/NetworkWireHarness.cpp ${ROOT}/src/misc/format.cpp"
for enet_source in callbacks compress host list packet peer protocol unix; do
    SOURCES="${SOURCES} ${ROOT}/src/enet/${enet_source}.c"
done

INCLUDES="-I${ROOT}/include -I${ROOT}/src -I${ROOT}/src/enet"

case "${MODE}" in
    wasm)
        command -v emcc >/dev/null 2>&1 || {
            echo "emcc not found; source the Emscripten SDK first" >&2
            exit 2
        }
        # shellcheck disable=SC2086
        emcc -std=c++17 -O1 -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sALLOW_MEMORY_GROWTH=1 \
             -sMAXIMUM_MEMORY=2147483648 -sEXIT_RUNTIME=1 \
             ${INCLUDES} ${SOURCES} -o "${OUT}/network-wire-harness.js"
        node "${OUT}/network-wire-harness.js"
        ;;
    native)
        CXX="${CXX:-c++}"
        SDL_CFLAGS="$(pkg-config --cflags sdl2 SDL2_mixer 2>/dev/null || sdl2-config --cflags)"
        SDL_LIBS="$(pkg-config --libs sdl2 SDL2_mixer 2>/dev/null || sdl2-config --libs)"
        # shellcheck disable=SC2086
        "${CXX}" -std=c++17 -O1 -g -fsanitize=address,undefined \
             ${INCLUDES} ${SDL_CFLAGS} ${SOURCES} ${SDL_LIBS} \
             -o "${OUT}/network-wire-harness"
        "${OUT}/network-wire-harness"
        ;;
    *)
        echo "usage: $0 [wasm|native]" >&2
        exit 2
        ;;
esac
