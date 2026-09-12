#!/usr/bin/env bash
#
# Runs two real crossplay peers against a real relay on loopback.
#
#   tests/relay/run-relay-transport-harness.sh             # both peers native
#   tests/relay/run-relay-transport-harness.sh diverge     # one peer's state is perturbed
#   tests/relay/run-relay-transport-harness.sh bulk        # large messages, integrity and order
#
# What this proves: HTTPS admission, the WebSocket handshake, the relay handshake, membership,
# routing, the host-driven phase change and the diagnostic channel, through the production code
# paths rather than through a mock. In "diverge" mode the perturbed peer's digest must be
# detected as a mismatch by both sides - a divergence test that fails if the detection is broken.
#
# "bulk" mode checks every byte and the sequence of large messages, paced below the production
# rate limit. This can expose corruption in frame continuations, but does not force partial
# socket writes. A successful run alone therefore does not verify that continuation path.
#
# Prerequisites:
#   * the relay's dependencies installed:  (cd tools/room-relay && npm ci)
#   * relay_transport_harness built:       cmake --build build --target relay_transport_harness
#
# tools/room-relay lives in the combined tree; this script expects it beside the game sources.
#
# The browser side cannot be driven from a shell; build the game for the web and open it with
#   ?relay=http://127.0.0.1:8787&relaydev=1
# against the same relay. The relay's own lifecycle log then shows runtime=browser for that peer.

set -euo pipefail

MODE="${1:-agree}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HARNESS="${RELAY_TRANSPORT_HARNESS:-${ROOT}/build/tests/relay_transport_harness}"
PORT="${RELAY_PORT:-8787}"
ENDPOINT="http://127.0.0.1:${PORT}"
WORK="$(mktemp -d)"

if [ ! -x "${HARNESS}" ]; then
    HARNESS="${ROOT}/build/bin/relay_transport_harness"
fi
if [ ! -x "${HARNESS}" ]; then
    echo "relay_transport_harness not found; build it with:" >&2
    echo "  cmake --build build --target relay_transport_harness" >&2
    exit 2
fi

cleanup() {
    for pid in "${GUEST_PID:-}" "${HOST_PID:-}"; do
        if [ -n "${pid}" ]; then
            kill "${pid}" 2>/dev/null || true
            wait "${pid}" 2>/dev/null || true
        fi
    done
    if [ -n "${RELAY_PID:-}" ]; then
        kill "${RELAY_PID}" 2>/dev/null || true
        wait "${RELAY_PID}" 2>/dev/null || true
    fi
    rm -rf "${WORK}"
}
trap cleanup EXIT

echo "== starting the relay on ${ENDPOINT}"
(
    cd "${ROOT}/tools/room-relay"
    export RELAY_PORT="${PORT}"
    exec node src/index.js --dev
) > "${WORK}/relay.log" 2>&1 &
RELAY_PID=$!

for _ in $(seq 1 50); do
    if ! kill -0 "${RELAY_PID}" 2>/dev/null; then
        cat "${WORK}/relay.log" >&2
        exit 1
    fi
    if curl -fsS "${ENDPOINT}/v1/health" > /dev/null 2>&1; then
        break
    fi
    sleep 0.2
done
curl -fsS "${ENDPOINT}/v1/health" > /dev/null
kill -0 "${RELAY_PID}"

# At most 256 messages, so indices 0 through 255 are unambiguous on arrival.
BULK_COUNT="${RELAY_BULK_COUNT:-48}"
BULK_BYTES="${RELAY_BULK_BYTES:-200000}"

HOST_FLAGS=()
GUEST_FLAGS=()
if [ "${MODE}" = "diverge" ]; then
    GUEST_FLAGS=(--corrupt --expect-mismatch)
elif [ "${MODE}" = "bulk" ]; then
    HOST_FLAGS=(--bulk="${BULK_COUNT}" --bulk-bytes="${BULK_BYTES}")
    GUEST_FLAGS=(--expect-bulk="${BULK_COUNT}" --bulk-bytes="${BULK_BYTES}")
fi

echo "== hosting"
"${HARNESS}" --endpoint="${ENDPOINT}" --dev --host --name=desktop --seconds=20 \
    ${HOST_FLAGS[@]+"${HOST_FLAGS[@]}"} > "${WORK}/host.log" 2>&1 &
HOST_PID=$!

ROOM=""
for _ in $(seq 1 100); do
    ROOM="$(grep -m1 '^ROOM code=' "${WORK}/host.log" 2>/dev/null | sed 's/^ROOM code=\([^ ]*\).*/\1/' || true)"
    if [ -n "${ROOM}" ]; then
        break
    fi
    sleep 0.1
done

if [ -z "${ROOM}" ]; then
    echo "the host never reported a room code:" >&2
    cat "${WORK}/host.log" >&2
    wait "${HOST_PID}" || true
    exit 1
fi
echo "== room ${ROOM}"

echo "== joining"
"${HARNESS}" --endpoint="${ENDPOINT}" --dev --join="${ROOM}" --name=guest --seconds=18 \
    ${GUEST_FLAGS[@]+"${GUEST_FLAGS[@]}"} > "${WORK}/guest.log" 2>&1 &
GUEST_PID=$!

GUEST_STATUS=0
wait "${GUEST_PID}" || GUEST_STATUS=$?
HOST_STATUS=0
wait "${HOST_PID}" || HOST_STATUS=$?

echo "== host"
cat "${WORK}/host.log"
echo "== guest"
cat "${WORK}/guest.log"
echo "== relay lifecycle"
cat "${WORK}/relay.log"

if [ "${MODE}" = "diverge" ]; then
    # The perturbed peer must report a mismatch, and so must the honest one.
    if ! grep -q 'DIGEST compare .*match=no' "${WORK}/host.log"; then
        echo "the honest peer did not detect the injected divergence" >&2
        exit 1
    fi
    if [ "${GUEST_STATUS}" -ne 0 ]; then
        echo "the perturbed peer did not report the divergence it was asked to expect" >&2
        exit 1
    fi
    # The honest peer is expected to "fail" here, because it saw a mismatch it did not expect.
    if [ "${HOST_STATUS}" -eq 0 ]; then
        echo "the honest peer reported success despite a divergence" >&2
        exit 1
    fi
    echo "== divergence detected by both peers, as intended"
    exit 0
fi

if [ "${HOST_STATUS}" -ne 0 ] || [ "${GUEST_STATUS}" -ne 0 ]; then
    echo "host exited ${HOST_STATUS}, guest exited ${GUEST_STATUS}" >&2
    exit 1
fi

if [ "${MODE}" = "bulk" ]; then
    if ! grep -q "bulkrecv=${BULK_COUNT} bulkcorrupt=0" "${WORK}/guest.log"; then
        echo "the receiving peer did not get every bulk message intact" >&2
        exit 1
    fi
    PEAK="$(sed -n 's/.*peakbacklog=\([0-9]*\).*/\1/p' "${WORK}/host.log" | tail -1)"
    if [ -z "${PEAK}" ] || [ "${PEAK}" = "0" ]; then
        # Not a failure: the messages arrived intact, which is the thing that matters. But the
        # socket accepted every frame whole, so the continuation path did not actually run on
        # this machine. Raise RELAY_BULK_COUNT or RELAY_BULK_BYTES to push harder.
        echo "== note: the outgoing queue never backed up, so partial writes were not exercised"
    else
        echo "== bulk transfer intact, peak outgoing backlog ${PEAK} bytes"
    fi
    exit 0
fi

echo "== both peers agreed"
