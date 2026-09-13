/** P2PKit RTCTransport, hardened for direct-only DuneCity play. See ../../UPSTREAM.md. */
import type { Transport, TransportEvents } from './types.js'
import type { SignallingChannel, SignallingMessage } from '../signalling/types.js'
import type { RTCBackend } from '../backends/index.js'
import type { PeerId } from '../utils/types.js'
import { Emitter } from '../utils/emitter.js'
import { Chunker, CHUNK_LIMITS } from '../framing/index.js'
import { randomId } from '../utils/id.js'
import { DEFAULT_ICE_SERVERS } from '../utils/ice.js'

export interface RTCTransportOptions {
  self: PeerId
  remote: PeerId
  signalling: SignallingChannel
  backend: RTCBackend
  iceServers?: RTCIceServer[]
  initiator: boolean
  chunkSize?: number
  label?: string
}
const MAX_BUFFER = 1 << 20
const MAX_OUTGOING = 4 << 20
const MAX_SIGNALS = 128
const MAX_SDP = 65_536
const MAX_CANDIDATE = 2048
const CONNECT_TIMEOUT = 30_000
const SEND_TIMEOUT = 10_000

/** Reject TURN configuration and relay candidates, including ones embedded in SDP. */
export function directIceServers(servers: RTCIceServer[]): RTCIceServer[] {
  if (!Array.isArray(servers) || servers.length > 8) throw new Error('Invalid STUN configuration')
  return servers.map(server => {
    const urls = typeof server.urls === 'string' ? [server.urls] : server.urls
    if (!Array.isArray(urls) || urls.length === 0 || urls.length > 8 || server.username || server.credential ||
        urls.some(url => typeof url !== 'string' || url.length > 256 ||
          !/^stuns?:[a-zA-Z0-9.\[\]:-]+$/.test(url))) throw new Error('Only STUN is supported for direct play')
    return { urls: [...urls] }
  })
}
export function validateDirectCandidate(candidate: unknown): asserts candidate is RTCIceCandidateInit {
  if (!candidate || typeof candidate !== 'object' || Array.isArray(candidate)) throw new Error('Invalid ICE candidate')
  const { candidate: value, sdpMid, sdpMLineIndex } = candidate as RTCIceCandidateInit
  if (typeof value !== 'string' || value.length > MAX_CANDIDATE || /[\r\n\0]/.test(value) ||
      (value !== '' && (!value.startsWith('candidate:') || /\styp\s+relay(?:\s|$)/.test(value) || !/\styp (host|srflx|prflx)(?:\s|$)/.test(value))) ||
      (sdpMid !== undefined && sdpMid !== null && (typeof sdpMid !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(sdpMid))) ||
      (sdpMLineIndex !== undefined && sdpMLineIndex !== null &&
       (!Number.isInteger(sdpMLineIndex) || sdpMLineIndex < 0 || sdpMLineIndex > 16))) throw new Error('Invalid direct ICE candidate')
}
export function validateDirectDescription(description: unknown): asserts description is RTCSessionDescriptionInit {
  if (!description || typeof description !== 'object') throw new Error('Invalid session description')
  const { type, sdp } = description as RTCSessionDescriptionInit
  if ((type !== 'offer' && type !== 'answer') || typeof sdp !== 'string' || sdp.length > MAX_SDP ||
      sdp.includes('\0') || !/^v=0\r?\n/.test(sdp) || !/^a=fingerprint:sha-256 (?:[0-9A-Fa-f]{2}:){31}[0-9A-Fa-f]{2}\r?$/m.test(sdp) ||
      (sdp.match(/^a=fingerprint:/gm)?.length ?? 0) !== 1 ||
      (sdp.match(/^m=application /gm)?.length ?? 0) !== 1 || /^m=(?!application )/m.test(sdp)) throw new Error('Invalid data-channel description')
  let count = 0
  for (const line of sdp.split(/\r?\n/)) {
    if (line.startsWith('a=candidate:')) {
      if (++count > MAX_SIGNALS) throw new Error('Too many ICE candidates')
      validateDirectCandidate({ candidate: line.slice(2) })
    }
  }
}

export class RTCTransport<T = unknown> implements Transport<T> {
  readonly remote: PeerId
  readonly name = 'rtc'
  private readonly self: PeerId
  private readonly signalling: SignallingChannel
  private readonly emitter = new Emitter<TransportEvents<T>>()
  private readonly chunker: Chunker
  private readonly pc: RTCPeerConnection
  private readonly initiator: boolean
  private readonly label: string
  private channel?: RTCDataChannel
  private remoteDescriptionSet = false
  private readonly pendingCandidates: RTCIceCandidateInit[] = []
  private signalChain = Promise.resolve()
  private signalCount = 0
  private candidateCount = 0
  private sendChain = Promise.resolve()
  private outgoingBytes = 0
  private outgoingCount = 0
  private closed = false
  private opened = false
  private readonly started = performance.now()
  private readonly timer: ReturnType<typeof setInterval>
  private unsubscribe?: () => void

  constructor(options: RTCTransportOptions) {
    this.self = options.self
    this.remote = options.remote
    this.signalling = options.signalling
    this.initiator = options.initiator
    this.label = options.label ?? 'p2pkit'
    this.chunker = new Chunker({ maxPacketSize: options.chunkSize })
    this.pc = new options.backend.RTCPeerConnection({ iceServers: directIceServers(options.iceServers ?? DEFAULT_ICE_SERVERS) })
    this.pc.onicecandidate = ev => {
      if (this.closed || !ev.candidate) return
      try {
        const candidate = ev.candidate.toJSON()
        validateDirectCandidate(candidate)
        this.signalling.send({ iceCandidate: candidate, from: this.self, to: this.remote })
      } catch { this.fail('Could not exchange direct connection details') }
    }
    this.pc.onconnectionstatechange = () => {
      if (['failed', 'closed', 'disconnected'].includes(this.pc.connectionState)) this.disconnect()
    }
    const unsubscribe = this.signalling.onMessage(this.onSignal) as unknown
    if (typeof unsubscribe === 'function') this.unsubscribe = unsubscribe as () => void
    this.timer = setInterval(() => {
      try {
        if (!this.opened && performance.now() - this.started > CONNECT_TIMEOUT) {
          this.fail('Could not connect directly. This network may block peer-to-peer connections.')
        }
        this.chunker.checkDeadline()
      } catch { this.fail('A peer sent an incomplete message') }
    }, 1000)
    ;(this.timer as unknown as { unref?: () => void }).unref?.()
    if (this.initiator) {
      this.setupChannel(this.pc.createDataChannel(this.label, { ordered: true }))
      void this.negotiate().catch(() => this.fail('Direct connection negotiation failed'))
    } else this.pc.ondatachannel = ev => this.setupChannel(ev.channel)
  }

  get bufferedAmount(): number { return this.outgoingBytes + (this.channel?.bufferedAmount ?? 0) }
  on<E extends keyof TransportEvents<T>>(event: E, handler: TransportEvents<T>[E]): void { this.emitter.on(event, handler) }

  async send(value: T): Promise<void> {
    if (this.closed || !this.opened) throw new Error('Direct connection is not open')
    const data = JSON.stringify(value)
    if (typeof data !== 'string' || data.length > CHUNK_LIMITS.messageBytes) throw new Error('Message too large')
    const bytes = new TextEncoder().encode(data).length
    if (bytes > CHUNK_LIMITS.messageBytes || bytes > MAX_OUTGOING - this.outgoingBytes || this.outgoingCount >= 128) {
      this.fail('Direct connection send queue is full')
      throw new Error('Direct connection send queue is full')
    }
    this.outgoingBytes += bytes
    this.outgoingCount++
    const result = this.sendChain.then(async () => {
      const deadline = performance.now() + SEND_TIMEOUT
      for (const packet of this.chunker.split(randomId(8), data)) {
        const channel = this.channel
        while (!this.closed && channel?.readyState === 'open' && channel.bufferedAmount > MAX_BUFFER) {
          if (performance.now() > deadline) throw new Error('Direct connection stalled')
          await new Promise(resolve => setTimeout(resolve, 10))
        }
        if (this.closed || channel?.readyState !== 'open') throw new Error('Direct connection closed')
        channel.send(packet)
      }
    }).finally(() => { this.outgoingBytes -= bytes; this.outgoingCount-- })
    this.sendChain = result.catch(() => this.fail('Direct connection could not deliver a message'))
    return result
  }

  disconnect(): void {
    if (this.closed) return
    this.closed = true
    clearInterval(this.timer)
    this.unsubscribe?.()
    this.pendingCandidates.length = 0
    this.chunker.reset()
    this.pc.onicecandidate = null
    this.pc.ondatachannel = null
    this.pc.onconnectionstatechange = null
    try { this.channel?.close() } catch { /* already closed */ }
    try { this.pc.close() } catch { /* already closed */ }
    this.emitter.emit('disconnect')
  }
  private fail(message: string): void {
    if (this.closed) return
    try { this.emitter.emit('error', new Error(message)) } finally { this.disconnect() }
  }
  private async negotiate(): Promise<void> {
    await this.signalling.ready
    if (this.closed) return
    const offer = await this.pc.createOffer()
    if (this.closed) return
    await this.pc.setLocalDescription(offer)
    this.sendDescription()
  }
  private sendDescription(): void {
    if (this.closed) return
    const description = this.pc.localDescription
    if (!description) throw new Error('Missing local description')
    validateDirectDescription(description)
    this.signalling.send({ description: { type: description.type, sdp: description.sdp }, from: this.self, to: this.remote })
  }
  private onSignal = (message: SignallingMessage): void => {
    if (this.closed || !message || typeof message !== 'object' || !('to' in message) ||
        message.from !== this.remote || message.to !== this.self) return
    if (++this.signalCount > MAX_SIGNALS) { this.signalCount--; this.fail('Too many connection messages'); return }
    this.signalChain = this.signalChain.then(() => this.handleSignal(message))
      .catch(() => this.fail('Invalid direct connection details')).finally(() => { this.signalCount-- })
  }
  private async handleSignal(message: SignallingMessage): Promise<void> {
    if (this.closed) return
    if ('description' in message) {
      validateDirectDescription(message.description)
      if (this.remoteDescriptionSet || message.description.type !== (this.initiator ? 'answer' : 'offer')) throw new Error('Unexpected description')
      await this.pc.setRemoteDescription(message.description)
      if (this.closed) return
      this.remoteDescriptionSet = true
      for (const candidate of this.pendingCandidates.splice(0)) {
        await this.pc.addIceCandidate(candidate)
        if (this.closed) return
      }
      if (!this.initiator) {
        const answer = await this.pc.createAnswer()
        if (this.closed) return
        await this.pc.setLocalDescription(answer)
        this.sendDescription()
      }
    } else if ('iceCandidate' in message) {
      validateDirectCandidate(message.iceCandidate)
      if (++this.candidateCount > MAX_SIGNALS) throw new Error('Too many candidates')
      if (this.remoteDescriptionSet) await this.pc.addIceCandidate(message.iceCandidate)
      else this.pendingCandidates.push(message.iceCandidate)
    } else throw new Error('Unexpected connection message')
  }
  private setupChannel(channel: RTCDataChannel): void {
    if (this.closed || this.channel || channel.label !== this.label || !channel.ordered ||
        channel.maxRetransmits !== null || channel.maxPacketLifeTime !== null) {
      channel.close()
      this.fail('Invalid direct game channel')
      return
    }
    this.channel = channel
    channel.binaryType = 'arraybuffer'
    channel.onopen = () => {
      if (this.closed || this.opened) return
      this.opened = true
      this.emitter.emit('connect')
    }
    channel.onmessage = ev => this.onData(ev.data)
    channel.onclose = () => this.disconnect()
    channel.onerror = () => this.fail('Direct game channel failed')
    if (channel.readyState === 'open') queueMicrotask(() => channel.onopen?.(new Event('open')))
  }
  private onData(data: unknown): void {
    if (this.closed || !this.opened) return
    try {
      if ((typeof data !== 'string' && !(data instanceof ArrayBuffer)) ||
          (typeof data === 'string' ? data.length : data.byteLength) > 131_072) throw new Error('Oversized fragment')
      const raw = typeof data === 'string' ? data : new TextDecoder('utf-8', { fatal: true }).decode(data)
      const full = this.chunker.ingest(JSON.parse(raw))
      if (full !== undefined) this.emitter.emit('message', JSON.parse(full) as T)
    } catch { this.fail('A peer sent an invalid game frame') }
  }
}
