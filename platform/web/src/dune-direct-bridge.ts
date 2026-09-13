/**
 * The browser half of a direct match.
 *
 * The game is WebAssembly and cannot hold an RTCPeerConnection, so this module owns the
 * connections and exposes a queue-shaped, string-only surface that C++ can call synchronously
 * (src/Network/DirectPeerConnectionEmscripten.cpp). Nothing here interprets the game protocol:
 * what crosses this boundary is the envelope text defined in include/Network/P2PWireFraming.h,
 * and it is passed through opaquely in both directions.
 *
 * The transport itself is P2PKit's hardened RTCTransport, used directly - not its mesh, broadcast
 * or RPC layers, and never a TURN server. Fragmentation belongs to RTCTransport's Chunker, which
 * is why this module hands it a whole value and never a fragment: the native side fragments
 * exactly once too, with the same rules, so the two interoperate.
 */

import { RTCTransport } from "../p2pkit/src/transports/rtc.ts"

type SignallingMessage =
  | { announce: true; from: string }
  | { description: { type: string; sdp: string }; from: string; to: string }
  | { iceCandidate: { candidate: string; sdpMid?: string | null; sdpMLineIndex?: number | null }; from: string; to: string }

/**
 * Mirrors DirectPeerConnection::State. The numbers cross the WebAssembly boundary, so they are
 * written out rather than left to an enum's ordering.
 */
const State = { New: 0, Connecting: 1, Connected: 2, Failed: 3, Closed: 4 } as const
type State = (typeof State)[keyof typeof State]

/** Bounds for what C++ may be handed back; the native side bounds its own input as well. */
const MAX_QUEUED_VALUES = 4096
const MAX_QUEUED_VALUE_BYTES = 4 * 1024 * 1024
const MAX_QUEUED_SIGNALS = 256
const MAX_SIGNAL_CHARS = 65536

interface Connection {
  transport: RTCTransport<string>
  deliver: ((message: SignallingMessage) => void) | null
  ready: () => void
  signals: string[]
  values: string[]
  valueBytes: number
  state: State
  error: string
}

const connections = new Map<number, Connection>()
let nextHandle = 1

/**
 * "<sdpMid>|<candidate>", the same shape libdatachannel produces and accepts.
 *
 * A media id of "0" stands in for a missing one: a data-only peer connection has exactly one
 * media section, and both the hardened transport and the native side refuse an empty id.
 */
function encodeCandidate(candidate: { candidate: string; sdpMid?: string | null }): string {
  return `${candidate.sdpMid || "0"}|${candidate.candidate}`
}

function decodeCandidate(payload: string): { candidate: string; sdpMid: string } | null {
  const split = payload.indexOf("|")
  if (split < 0) return null
  const sdpMid = payload.slice(0, split)
  const candidate = payload.slice(split + 1)
  if (!candidate || candidate.length > MAX_SIGNAL_CHARS) return null
  return { candidate, sdpMid }
}

/**
 * Ends a connection that can no longer be trusted to be in step.
 *
 * The queues are emptied rather than trimmed - handing the simulation a prefix of a command
 * stream is worse than handing it nothing - and the peer connection is closed immediately, so a
 * failed link cannot keep gathering candidates or accepting data while C++ notices.
 */
function fail(connection: Connection, reason: string): void {
  if (connection.state !== State.Closed) connection.state = State.Failed
  if (!connection.error) connection.error = reason
  connection.values.length = 0
  connection.valueBytes = 0
  connection.signals.length = 0
  try {
    connection.transport?.disconnect()
  } catch {
    /* already gone */
  }
}

/** True while this connection may still be given remote signalling. */
function usable(connection: Connection | undefined): connection is Connection {
  return (
    connection !== undefined &&
    connection.state !== State.Failed &&
    connection.state !== State.Closed &&
    connection.deliver !== null
  )
}

/**
 * Reads the ICE configuration, or refuses it.
 *
 * Quietly dropping the entries it does not like would connect with a configuration nobody chose
 * and nobody could see - and "it worked, but without the STUN server you asked for" is the kind
 * of success that only shows up as a player who cannot connect across a NAT.
 */
function parseIceServers(iceServersJson: string): RTCIceServer[] | null {
  let parsed: unknown
  try {
    parsed = JSON.parse(iceServersJson)
  } catch {
    return null
  }
  if (!Array.isArray(parsed) || parsed.length > 4) return null
  const servers: RTCIceServer[] = []
  for (const url of parsed) {
    if (typeof url !== "string" || !/^stuns?:[a-zA-Z0-9.\[\]:-]+$/.test(url)) return null
    servers.push({ urls: url })
  }
  return servers
}

function create(iceServersJson: string, initiator: boolean, label: string): number {
  const iceServers = parseIceServers(iceServersJson)
  if (iceServers === null) return 0

  const handle = nextHandle++
  let resolveReady: () => void = () => {}
  const ready = new Promise<void>(resolve => {
    resolveReady = resolve
  })

  const connection: Connection = {
    transport: undefined as unknown as RTCTransport<string>,
    deliver: null,
    ready: resolveReady,
    signals: [],
    values: [],
    valueBytes: 0,
    state: State.New,
    error: "",
  }

  // The signalling channel is this bridge: outbound messages become strings for C++ to post to
  // the signaling service, inbound ones are injected by C++ after it has verified the sender's
  // fingerprint against what the service admitted for this pair.
  const signalling = {
    ready,
    send(message: SignallingMessage): void {
      if (connection.signals.length >= MAX_QUEUED_SIGNALS) {
        fail(connection, "local signal queue overflowed")
        return
      }
      if ("description" in message) {
        const sdp = message.description.sdp ?? ""
        if (sdp.length > MAX_SIGNAL_CHARS) {
          fail(connection, "local description too large")
          return
        }
        connection.signals.push(`${message.description.type === "answer" ? "answer" : "offer"}|${sdp}`)
      } else if ("iceCandidate" in message) {
        connection.signals.push(`candidate|${encodeCandidate(message.iceCandidate)}`)
      }
    },
    onMessage(handler: (message: SignallingMessage) => void): () => void {
      connection.deliver = handler
      // The transport calls this when it disconnects. Dropping the handler here means a late
      // signal cannot be delivered into a connection that has already gone.
      return () => {
        if (connection.deliver === handler) connection.deliver = null
      }
    },
  }

  connection.transport = new RTCTransport<string>({
    self: "local",
    remote: "remote",
    signalling,
    backend: { RTCPeerConnection: globalThis.RTCPeerConnection },
    // Exactly the servers the signaling service named, and no others. An empty list means host
    // candidates only, which is the right answer on a LAN; falling back to P2PKit's public STUN
    // defaults would contact a third party this game never told the player about.
    iceServers,
    initiator,
    label,
  })

  connection.transport.on("connect", () => {
    if (connection.state !== State.Failed && connection.state !== State.Closed) {
      connection.state = State.Connected
    }
  })
  connection.transport.on("message", value => {
    // A callback that is already queued cannot refill a connection that has been failed or
    // closed: its queues were emptied on purpose.
    if (connection.state === State.Failed || connection.state === State.Closed) return
    if (typeof value !== "string") {
      fail(connection, "peer sent a non-string payload")
      return
    }
    if (
      connection.values.length >= MAX_QUEUED_VALUES ||
      connection.valueBytes + value.length > MAX_QUEUED_VALUE_BYTES
    ) {
      fail(connection, "this computer could not keep up with a player's messages")
      return
    }
    connection.valueBytes += value.length
    connection.values.push(value)
  })
  connection.transport.on("disconnect", () => {
    if (connection.state !== State.Failed) connection.state = State.Closed
  })
  connection.transport.on("error", error => {
    fail(connection, error instanceof Error ? error.message : "data channel error")
  })

  // The channel is usable the moment it is created: C++ posts whatever comes out of it.
  resolveReady()
  connection.state = State.Connecting
  connections.set(handle, connection)
  return handle
}

const bridge = {
  create,

  /** Applies a remote offer or answer. The C++ side has already checked the fingerprint. */
  setRemoteDescription(handle: number, kind: string, sdp: string): boolean {
    const connection = connections.get(handle)
    if (!usable(connection) || sdp.length > MAX_SIGNAL_CHARS) return false
    connection.deliver!({
      description: { type: kind === "answer" ? "answer" : "offer", sdp },
      from: "remote",
      to: "local",
    })
    return true
  },

  addRemoteCandidate(handle: number, payload: string): boolean {
    const connection = connections.get(handle)
    if (!usable(connection)) return false
    const candidate = decodeCandidate(payload)
    if (!candidate) return false
    connection.deliver!({ iceCandidate: candidate, from: "remote", to: "local" })
    return true
  },

  /** Sends one envelope. The text is a JSON value; RTCTransport fragments it. */
  sendValue(handle: number, text: string): boolean {
    const connection = connections.get(handle)
    if (!connection || connection.state !== State.Connected) return false
    if (connection.transport.bufferedAmount > 4 * 1024 * 1024) return false
    let value: unknown
    try {
      value = JSON.parse(text)
    } catch {
      return false
    }
    if (typeof value !== "string") return false
    void connection.transport.send(value).catch((error: unknown) => {
      fail(connection, error instanceof Error ? error.message : "send failed")
    })
    return true
  },

  /** Takes one received envelope as JSON text, or null. */
  pollValue(handle: number): string | null {
    const connection = connections.get(handle)
    if (!connection || connection.values.length === 0) return null
    const value = connection.values.shift() as string
    connection.valueBytes -= value.length
    return JSON.stringify(value)
  },

  /** Takes one locally produced signal as "<kind>|<payload>", or null. */
  pollSignal(handle: number): string | null {
    const connection = connections.get(handle)
    if (!connection || connection.signals.length === 0) return null
    return connection.signals.shift() as string
  },

  bufferedAmount(handle: number): number {
    const connection = connections.get(handle)
    return connection ? connection.transport.bufferedAmount : 0
  },

  state(handle: number): number {
    const connection = connections.get(handle)
    return connection ? connection.state : State.Closed
  },

  lastError(handle: number): string {
    const connection = connections.get(handle)
    return connection ? connection.error : ""
  },

  close(handle: number): void {
    const connection = connections.get(handle)
    if (!connection) return
    try {
      connection.transport.disconnect()
    } catch {
      /* already gone */
    }
    connection.state = State.Closed
    connection.values.length = 0
    connection.signals.length = 0
    connections.delete(handle)
  },
}

declare global {
  // eslint-disable-next-line no-var
  var DuneDirectBridge: typeof bridge | undefined
}

globalThis.DuneDirectBridge = bridge

export default bridge
export type DuneDirectBridge = typeof bridge
