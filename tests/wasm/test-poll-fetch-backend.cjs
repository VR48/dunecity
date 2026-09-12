'use strict';
// Test the generated EM_JS functions, including C preprocessor/stringification effects.
// Usage: node tests/wasm/test-poll-fetch-backend.cjs BUILD/bin/dunecity.js
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(process.argv[2], 'utf8');
const names = ['duneRelayPollStart', 'duneRelayPollState', 'duneRelayPollStatus',
  'duneRelayPollLength', 'duneRelayPollCopy', 'duneRelayPollRelease'];
function extract(name) {
  const start = source.indexOf('function ' + name + '(');
  assert.ok(start >= 0, name);
  // Generated production code has no braces inside strings in these six functions.
  let level = 0, begun = false;
  for (let i = source.indexOf('{', start); i < source.length; ++i) {
    if (source[i] === '{') { level++; begun = true; }
    if (source[i] === '}') level--;
    if (begun && level === 0) return source.slice(start, i + 1);
  }
  throw new Error('Unclosed function');
}
const code = names.map(extract).join('\n');
async function run(response, expected, max = 1024) {
  let options;
  const context = vm.createContext({ Uint8Array, AbortController, setTimeout, clearTimeout,
    UTF8ToString: x => x, HEAPU8: new Uint8Array(1024),
    fetch: async (url, opts) => { options = opts; return response; } });
  vm.runInContext(code, context);
  const handle = context.duneRelayPollStart('http://127.0.0.1/poll/open', '', 0, 0, 1000, max);
  for (let i = 0; i < 100 && context.duneRelayPollState(handle) === 0; i++) {
    await new Promise(resolve => setTimeout(resolve, 2));
  }
  assert.equal(context.duneRelayPollState(handle), expected);
  assert.equal(options.credentials, 'omit');
  assert.equal(options.redirect, 'error');
  if (expected === 1) assert.equal(context.duneRelayPollLength(handle), 65);
  context.duneRelayPollRelease(handle);
  assert.equal(vm.runInContext('Object.keys(globalThis.__duneRelayPoll.requests).length', context), 0);
}
(async () => {
  await run(new Response('a'.repeat(64) + '\n', { headers: { 'Content-Length': '65' } }), 1);
  let cancelled = false;
  const oversized = new ReadableStream({ pull(c) { c.enqueue(new Uint8Array(600)); }, cancel() { cancelled = true; } });
  await run(new Response(oversized), 4);
  assert.equal(cancelled, true);
  await run(new Response('small', { headers: { 'Content-Length': '999999999' } }), 4);
  console.log('PASS: generated browser fetch accepts a valid handshake, bounds streamed/declared responses, and releases requests');
})().catch(error => { console.error(error); process.exitCode = 1; });
