/** P2PKit framing, hardened by DuneCity. See ../../UPSTREAM.md. */
export interface ChunkPacket { id: string; i: number; n: number; part: string }
export interface ChunkerOptions { maxPacketSize?: number }

export const CHUNK_LIMITS = Object.freeze({
  packetChars: 16_000,
  messageBytes: 1_048_576,
  fragments: 128,
  pendingGroups: 16,
  pendingBytes: 4_194_304,
  lifetimeMs: 15_000,
})
interface Partial {
  parts: (string | undefined)[]
  received: number
  bytes: number
  created: number
}
const encoder = new TextEncoder()

/** Invalid or exhausted framing fails the connection; never drop a lockstep prefix. */
export class Chunker {
  readonly maxPacketSize: number
  private readonly inbox = new Map<string, Partial>()
  private bytes = 0

  constructor(options: ChunkerOptions = {}) {
    this.maxPacketSize = options.maxPacketSize ?? CHUNK_LIMITS.packetChars
    if (!Number.isSafeInteger(this.maxPacketSize) || this.maxPacketSize < 1 ||
        this.maxPacketSize > CHUNK_LIMITS.packetChars) throw new Error('Invalid chunk size')
  }

  *split(id: string, data: string): Generator<string> {
    if (!validId(id) || typeof data !== 'string' || data.length > CHUNK_LIMITS.messageBytes ||
        encoder.encode(data).length > CHUNK_LIMITS.messageBytes) throw new Error('Message too large')
    const n = Math.max(1, Math.ceil(data.length / this.maxPacketSize))
    if (n > CHUNK_LIMITS.fragments) throw new Error('Too many fragments')
    for (let i = 0; i < n; ++i) {
      yield JSON.stringify({ id, i, n, part: data.slice(i * this.maxPacketSize, (i + 1) * this.maxPacketSize) })
    }
  }

  /** Called by the transport's timer as well as on receipt, including when a peer goes silent. */
  checkDeadline(now = performance.now()): void {
    for (const entry of this.inbox.values()) {
      if (now - entry.created >= CHUNK_LIMITS.lifetimeMs) throw new Error('Incomplete message expired')
    }
  }

  ingest(packet: unknown): string | undefined {
    this.checkDeadline()
    if (!packet || typeof packet !== 'object' || Array.isArray(packet)) throw new Error('Invalid fragment')
    const { id, i, n, part } = packet as ChunkPacket
    if (!validId(id) || !Number.isSafeInteger(n) || n < 1 || n > CHUNK_LIMITS.fragments ||
        !Number.isSafeInteger(i) || i < 0 || i >= n || typeof part !== 'string' ||
        part.length > this.maxPacketSize) throw new Error('Invalid fragment')
    let entry = this.inbox.get(id)
    if (entry && entry.parts.length !== n) throw new Error('Fragment count changed')
    const bytes = encoder.encode(part).length
    if (n === 1) return part
    if (!entry) {
      if (this.inbox.size >= CHUNK_LIMITS.pendingGroups) throw new Error('Too many incomplete messages')
      entry = { parts: new Array<string | undefined>(n), received: 0, bytes: 0, created: performance.now() }
      this.inbox.set(id, entry)
    }
    const previous = entry.parts[i]
    if (previous !== undefined) {
      if (previous !== part) throw new Error('Conflicting fragment')
      return undefined
    }
    if (bytes > CHUNK_LIMITS.messageBytes - entry.bytes || bytes > CHUNK_LIMITS.pendingBytes - this.bytes) {
      throw new Error('Reassembly budget exceeded')
    }
    entry.parts[i] = part
    entry.received++
    entry.bytes += bytes
    this.bytes += bytes
    if (entry.received !== n) return undefined
    this.inbox.delete(id)
    this.bytes -= entry.bytes
    return entry.parts.join('')
  }

  reset(): void { this.inbox.clear(); this.bytes = 0 }
}
function validId(id: unknown): id is string {
  return typeof id === 'string' && /^[A-Za-z0-9_-]{1,64}$/.test(id)
}
