import type { PeerId } from "../utils/types.js"

/**
 * Messages exchanged over a {@link SignallingChannel} to bootstrap WebRTC. The
 * channel only needs to relay these between peers in a room — it never inspects
 * media (README §8).
 */
export type SignallingMessage =
  | { announce: true; from: PeerId }
  | { description: RTCSessionDescriptionInit; from: PeerId; to: PeerId } // offer & answer
  | { iceCandidate: RTCIceCandidateInit; from: PeerId; to: PeerId }

/**
 * Transports the WebRTC handshake. Swap {@link WebSocketSignalling} for your own
 * server, a pub/sub topic, or manual copy-paste by implementing this interface.
 */
export interface SignallingChannel {
  /** Send a message to the room; the server relays it to the relevant peer(s). */
  send(message: SignallingMessage): void
  /** Register a handler for incoming messages. */
  onMessage(handler: (message: SignallingMessage) => void): void
  /** Resolves once the channel is ready to send. */
  readonly ready: Promise<void>
}
