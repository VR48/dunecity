#!/usr/bin/env bash
#
# Builds and runs the standalone relay wire harness.
#
#   tests/wasm/run-relay-wire-harness.sh wasm     # emcc + node (wasm32, 32-bit size_t)
#   tests/wasm/run-relay-wire-harness.sh native   # host compiler (LP64), ASan/UBSan
#
# The wasm run is the one that matters: the browser client is wasm32, where size_t is 32 bits
# and an additive length check wraps. The native run exists so the same harness can be sanity
# checked without an Emscripten toolchain.
#
# Unlike the ENet wire harness this needs no SDL, no ENet and no game data: the relay protocol,
# the endpoint validation, the admission parser and the state digest are all self-contained
# headers.
#
# Run from anywhere. Exits non-zero if any check fails.

set -euo pipefail

MODE="${1:-wasm}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${ROOT}/build/relay-harness"

mkdir -p "${OUT}"

SOURCE="${ROOT}/tests/wasm/RelayWireHarness.cpp"
INCLUDES=("-I${ROOT}/include" "-I${ROOT}/build/include")

case "${MODE}" in
    wasm)
        command -v em++ >/dev/null 2>&1 || {
            echo "em++ not found; source the Emscripten SDK first" >&2
            exit 2
        }
        em++ -std=c++17 -O1 -Wall -Wextra \
             -sEXIT_RUNTIME=1 -sENVIRONMENT=node \
             "${INCLUDES[@]}" "${SOURCE}" \
             -o "${OUT}/relay-wire-harness.js"
        node "${OUT}/relay-wire-harness.js"
        ;;
    native)
        CXX="${CXX:-c++}"
        "${CXX}" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
             "${INCLUDES[@]}" "${SOURCE}" \
             -o "${OUT}/relay-wire-harness"
        "${OUT}/relay-wire-harness"
        ;;
    *)
        echo "usage: $0 [wasm|native]" >&2
        exit 2
        ;;
esac
