/**
 * Cross-checks the native framing against the browser's, using the vendored chunker itself.
 *
 * The strings below are the exact ones tests/P2PWireFramingTestCase asserts the native encoder
 * produces. Feeding the same literals to platform/web/p2pkit/src/framing/index.ts closes the
 * loop: if either side changes its output, one of the two test suites fails instead of a browser
 * and a native client quietly failing to understand each other.
 *
 *   node --experimental-strip-types tools/p2p-interop/framing-vectors.mjs
 */
import { Chunker, CHUNK_LIMITS } from '../../platform/web/p2pkit/src/framing/index.ts'

let failures = 0
function check(name, actual, expected) {
  const ok = actual === expected
  if (!ok) {
    failures++
    console.error(`FAIL ${name}\n  expected ${JSON.stringify(expected)}\n  actual   ${JSON.stringify(actual)}`)
  } else {
    console.log(`ok   ${name}`)
  }
}

// 1. What C++ emits must be what the browser emits, byte for byte.
const chunker = new Chunker({})
check(
  'single fragment matches the native encoder',
  [...chunker.split('abcdef0123456789', JSON.stringify('g:0:0:0001ff7f'))][0],
  '{"id":"abcdef0123456789","i":0,"n":1,"part":"\\"g:0:0:0001ff7f\\""}',
)
check(
  'readiness envelope matches the native encoder',
  [...chunker.split('AB_cd-01', JSON.stringify('r:1,2,3:2,3'))][0],
  '{"id":"AB_cd-01","i":0,"n":1,"part":"\\"r:1,2,3:2,3\\""}',
)

// 2. What C++ emits must be what the browser accepts.
const reader = new Chunker({})
check(
  'the browser reassembles a native single fragment',
  reader.ingest(JSON.parse('{"id":"abcdef0123456789","i":0,"n":1,"part":"\\"g:0:0:0001ff7f\\""}')),
  JSON.stringify('g:0:0:0001ff7f'),
)

// 3. A full-size game packet: 262128 bytes of hex plus the envelope, split and reassembled.
const value = JSON.stringify('g:1:7:' + 'ab'.repeat(262128 / 2))
const packets = [...new Chunker({}).split('0123456789abcdef', value)]
check('a full-size packet stays inside the fragment bound',
      String(packets.length <= CHUNK_LIMITS.fragments), 'true')
const assembler = new Chunker({})
let assembled
for (const packet of packets) assembled = assembler.ingest(JSON.parse(packet)) ?? assembled
check('a full-size packet round trips', assembled, value)

// 4. The bounds are fatal in the browser too, which is what the native side relies on.
for (const [name, packet] of [
  ['zero fragments', { id: 'a', i: 0, n: 0, part: 'x' }],
  ['too many fragments', { id: 'a', i: 0, n: CHUNK_LIMITS.fragments + 1, part: 'x' }],
  ['index outside the group', { id: 'a', i: 2, n: 2, part: 'x' }],
  ['unacceptable group id', { id: 'a b', i: 0, n: 1, part: 'x' }],
]) {
  let threw = false
  try { new Chunker({}).ingest(packet) } catch { threw = true }
  check(`the browser refuses ${name}`, String(threw), 'true')
}

process.exit(failures === 0 ? 0 : 1)
