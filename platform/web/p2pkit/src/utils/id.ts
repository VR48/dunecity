/**
 * Cryptographically-random hex id, used for message ids, nonces and chunk
 * groups. Uses the universal Web Crypto API (`globalThis.crypto`), available in
 * browsers, Node 18+, Bun and Deno.
 */
export function randomId(bytes = 16): string {
  const buf = new Uint8Array(bytes)
  globalThis.crypto.getRandomValues(buf)
  let out = ""
  for (const b of buf) out += b.toString(16).padStart(2, "0")
  return out
}
