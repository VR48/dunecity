import type { PeerId } from "../utils/types.js"

/** Events every {@link Transport} emits (README §8). */
export type TransportEvents<Msg> = {
  connect: () => void
  message: (msg: Msg) => void
  disconnect: () => void
  error: (err: Error) => void
}

/**
 * A single point-to-point link to one remote peer. Add a link type beyond the
 * built-ins by implementing this interface (README §8). `P2PKit` drives
 * transports generically over the wire {@link ../wire.Frame} type.
 */
export interface Transport<Msg> {
  /** The peer on the other end. */
  readonly remote: PeerId
  /** Bytes queued but not yet flushed to the network. */
  readonly bufferedAmount: number
  /** Send a message; resolves once flushed (await it, or watch `bufferedAmount`). */
  send(message: Msg): Promise<void>
  on<E extends keyof TransportEvents<Msg>>(event: E, handler: TransportEvents<Msg>[E]): void
  /** Close the link. */
  disconnect(): void
}
