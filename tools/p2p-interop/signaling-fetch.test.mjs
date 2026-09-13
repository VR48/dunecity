/**
 * Regression test for the browser signaling HTTP backend's JavaScript.
 *
 * The JavaScript that actually runs in the browser lives inside EM_JS blocks in
 * src/Network/BoundedHttpClient.cpp, where nothing else can reach it: there is no Emscripten
 * toolchain here and no browser. So this extracts those exact blocks from the source and runs
 * them against stubbed fetch/AbortController/heap, which is enough to pin the properties that
 * matter and that a C++ compiler cannot check:
 *
 *   - a redirect is refused rather than followed, and credentials are never attached
 *   - an over-large response is refused by its declared length, and again while it streams if
 *     the declared length was a lie
 *   - a timeout aborts the request rather than leaving it outstanding
 *   - releasing a request in flight aborts it, and a promise that settles afterwards cleans up
 *     instead of writing into a released entry
 *   - the session token is a header and never appears in the URL
 *
 *   node --experimental-strip-types tools/p2p-interop/signaling-fetch.test.mjs
 */
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import assert from 'node:assert/strict'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const source = readFileSync(path.join(root, 'src/Network/BoundedHttpClient.cpp'), 'utf8')

/** Pulls one EM_JS block out of the C++ source: its parameter names and its body. */
function extractEmJs(name) {
  const marker = `EM_JS(`
  let at = 0
  while ((at = source.indexOf(marker, at)) >= 0) {
    const headerEnd = source.indexOf('{', at)
    const header = source.slice(at + marker.length, headerEnd)
    const parts = header.split(',')
    if (parts[1].trim() !== name) { at = headerEnd; continue }
    // The parameter list is everything between the first '(' after the name and its ')'.
    const argsStart = header.indexOf('(')
    const argsEnd = header.lastIndexOf(')')
    const args = header
      .slice(argsStart + 1, argsEnd)
      .split(',')
      .map(part => part.trim())
      .filter(part => part.length > 0 && part !== 'void')
      .map(part => part.replace(/[*]/g, ' ').trim().split(/\s+/).pop())
    // The body is the braced block, matched so that nested braces do not end it early.
    let depth = 0
    let index = headerEnd
    for (; index < source.length; index++) {
      if (source[index] === '{') depth++
      else if (source[index] === '}') { depth--; if (depth === 0) break }
    }
    return { args, body: source.slice(headerEnd + 1, index) }
  }
  throw new Error(`EM_JS ${name} not found`)
}

/** The Emscripten helpers the extracted JavaScript expects, stubbed. */
function makeRuntime() {
  const strings = new Map()
  let next = 1
  const heap = new Uint8Array(1 << 16)
  return {
    heap,
    pointerFor(text) { const pointer = next++; strings.set(pointer, text); return pointer },
    helpers: {
      UTF8ToString: pointer => strings.get(pointer) ?? '',
      HEAPU8: heap,
    },
  }
}

function compile(name, runtime) {
  const { args, body } = extractEmJs(name)
  const fn = new Function('UTF8ToString', 'HEAPU8', ...args, body)
  return (...callArgs) => fn(runtime.helpers.UTF8ToString, runtime.helpers.HEAPU8, ...callArgs)
}

const runtime = makeRuntime()
const start = compile('duneSignalStart', runtime)
const state = compile('duneSignalState', runtime)
const status = compile('duneSignalStatus', runtime)
const length = compile('duneSignalLength', runtime)
const release = compile('duneSignalRelease', runtime)

const settle = () => new Promise(resolve => setTimeout(resolve, 5))

let calls = []
let aborted = 0
class StubAbortController {
  constructor() { this.signal = { aborted: false } }
  abort() { aborted++; this.signal.aborted = true; this.onabort?.() }
}
globalThis.AbortController = StubAbortController

/** A response whose body streams in the given chunks. */
function streamingResponse({ status: code = 200, declared = null, chunks = [] }) {
  let index = 0
  return {
    status: code,
    headers: { get: name => (name === 'Content-Length' ? declared : null) },
    body: {
      getReader: () => ({
        read: async () => (index < chunks.length
          ? { done: false, value: new TextEncoder().encode(chunks[index++]) }
          : { done: true }),
        cancel: async () => {},
      }),
    },
  }
}

function install(handler) {
  calls = []
  aborted = 0
  globalThis.fetch = (url, options) => {
    calls.push({ url, options })
    return handler(url, options)
  }
}

let failures = 0
async function test(name, fn) {
  try { await fn(); console.log('ok  ', name) } catch (error) { failures++; console.error('FAIL', name, '\n ', error.message) }
}

const url = runtime.pointerFor('https://example.test/p2p/v1/p2p/poll')
const token = runtime.pointerFor('a'.repeat(64))
const empty = runtime.pointerFor('')

await test('the request refuses redirects and attaches no ambient credentials', async () => {
  install(async () => streamingResponse({ chunks: ['status=ok\ncursor=1\n'] }))
  const handle = start(url, token, empty, 0, 5000, 524288)
  await settle()
  const options = calls[0].options
  assert.equal(options.redirect, 'error')
  assert.equal(options.credentials, 'omit')
  assert.equal(options.cache, 'no-store')
  assert.equal(options.referrerPolicy, 'no-referrer')
  assert.equal(options.method, 'POST')
  assert.equal(options.headers['Content-Type'], 'application/x-www-form-urlencoded')
  // The credential is a header, and the URL never mentions it.
  assert.equal(options.headers['X-Dune-Session'], 'a'.repeat(64))
  assert.ok(!calls[0].url.includes('a'.repeat(64)))
  assert.equal(state(handle), 1)
  assert.equal(status(handle), 200)
  assert.equal(length(handle), 'status=ok\ncursor=1\n'.length)
  release(handle)
})

await test('a refused redirect is reported as a failure, not followed', async () => {
  // This is what the browser does with redirect:'error' - the promise rejects.
  install(async () => { throw new TypeError('Failed to fetch') })
  const handle = start(url, token, empty, 0, 5000, 524288)
  await settle()
  assert.equal(state(handle), 2)
  release(handle)
})

await test('a response that declares more than the bound is refused before it is read', async () => {
  let readerUsed = false
  install(async () => ({
    status: 200,
    headers: { get: () => '600000' },
    body: { getReader: () => { readerUsed = true; return { read: async () => ({ done: true }), cancel: async () => {} } } },
  }))
  const handle = start(url, token, empty, 0, 5000, 524288)
  await settle()
  assert.equal(state(handle), 4, 'expected the overflow state')
  assert.equal(readerUsed, false, 'the body must not be read at all')
  assert.ok(aborted > 0, 'the request must be aborted')
  release(handle)
})

await test('a response that lies about its length is refused while it streams', async () => {
  const chunk = 'x'.repeat(300000)
  install(async () => streamingResponse({ declared: '10', chunks: [chunk, chunk] }))
  const handle = start(url, token, empty, 0, 5000, 524288)
  await settle()
  assert.equal(state(handle), 4)
  assert.equal(length(handle), 0)
  release(handle)
})

await test('a request that never answers times out and is aborted', async () => {
  install(() => new Promise(() => {}))
  const handle = start(url, token, empty, 0, 10, 524288)
  await new Promise(resolve => setTimeout(resolve, 40))
  assert.ok(aborted > 0, 'the timeout must abort the request')
  // The stub fetch never settles, so the entry stays pending until the promise rejects; what
  // matters is that the abort happened and the entry is not reported as a completed answer.
  assert.notEqual(state(handle), 1)
  release(handle)
})

await test('releasing a request in flight aborts it and discards a late answer', async () => {
  let resolveFetch
  install(() => new Promise(resolve => { resolveFetch = resolve }))
  const handle = start(url, token, empty, 0, 5000, 524288)
  release(handle)
  assert.ok(aborted > 0, 'releasing must abort')
  resolveFetch(streamingResponse({ chunks: ['status=ok\ncursor=1\n'] }))
  await settle()
  // Nothing was written into the released entry, and the registry does not keep it.
  assert.equal(state(handle), 0)
  assert.equal(length(handle), 0)
  assert.equal(Object.keys(globalThis.__duneP2PSignal.requests).length, 0)
})

await test('a released handle cannot collide with a later one', async () => {
  install(async () => streamingResponse({ chunks: ['status=ok\n'] }))
  const first = start(url, token, empty, 0, 5000, 524288)
  release(first)
  const second = start(url, token, empty, 0, 5000, 524288)
  assert.notEqual(first, second)
  await settle()
  assert.equal(state(first), 0)
  assert.equal(state(second), 1)
  release(second)
})

process.exit(failures === 0 ? 0 : 1)
