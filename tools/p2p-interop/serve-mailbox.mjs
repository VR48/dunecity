/**
 * Loopback-only helper for the interoperability harness. Development tool, never deployed.
 *
 * It does two things: serve the page in this directory (plus the bundled bridge) and shuttle
 * signaling messages between the browser and the native harness through a directory. It carries
 * offers, answers and candidates and nothing else - it has no idea what a game packet is, and the
 * peers do not send it any. It binds 127.0.0.1 only.
 *
 *   node tools/p2p-interop/serve-mailbox.mjs --dir /tmp/dune-interop [--port 8799]
 */
import { createServer } from 'node:http'
import { mkdirSync, readdirSync, readFileSync, renameSync, writeFileSync, existsSync } from 'node:fs'
import { join, resolve, extname } from 'node:path'

const args = process.argv.slice(2)
const option = (name, fallback) => {
  const at = args.indexOf(name)
  return at >= 0 && at + 1 < args.length ? args[at + 1] : fallback
}
const mailbox = resolve(option('--dir', '/tmp/dune-interop'))
const port = Number(option('--port', '8799'))
const here = resolve(new URL('.', import.meta.url).pathname)
const root = resolve(here, '..', '..')

for (const box of ['to-browser', 'to-native']) mkdirSync(join(mailbox, box), { recursive: true })
const taken = new Set()
let sequence = 0

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css' }

createServer((request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1')
  const send = (status, body, type = 'application/json') => {
    response.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' })
    response.end(body)
  }

  if (url.pathname === '/recv') {
    const directory = join(mailbox, 'to-browser')
    const messages = []
    for (const name of readdirSync(directory).sort()) {
      if (!name.endsWith('.msg') || taken.has(name)) continue
      taken.add(name)
      const text = readFileSync(join(directory, name), 'utf8')
      const split = text.indexOf('\n')
      if (split < 0) continue
      messages.push({ kind: text.slice(0, split), payload: text.slice(split + 1) })
    }
    return send(200, JSON.stringify(messages))
  }

  if (url.pathname === '/send' && request.method === 'POST') {
    let body = ''
    request.on('data', chunk => {
      body += chunk
      if (body.length > 1 << 20) request.destroy()
    })
    request.on('end', () => {
      const { kind, payload } = JSON.parse(body)
      if (!['offer', 'answer', 'candidate'].includes(kind) || typeof payload !== 'string') {
        return send(400, '{"error":"bad signal"}')
      }
      const name = `b${String(sequence++).padStart(4, '0')}`
      const directory = join(mailbox, 'to-native')
      writeFileSync(join(directory, `${name}.partial`), `${kind}\n${payload}`)
      renameSync(join(directory, `${name}.partial`), join(directory, `${name}.msg`))
      send(200, '{"ok":true}')
    })
    return
  }

  // Static files: this directory and the bundled bridge only.
  const candidates = {
    '/': join(here, 'browser-peer.html'),
    '/browser-peer.html': join(here, 'browser-peer.html'),
    '/p2p-direct.js': join(root, 'platform', 'web', 'p2p-direct.js'),
  }
  const file = candidates[url.pathname]
  if (!file || !existsSync(file)) return send(404, 'not found', 'text/plain')
  send(200, readFileSync(file), types[extname(file)] ?? 'application/octet-stream')
}).listen(port, '127.0.0.1', () => {
  console.log(`interop mailbox on http://127.0.0.1:${port}/ using ${mailbox}`)
})
