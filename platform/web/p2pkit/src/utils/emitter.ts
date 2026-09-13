/** A map of event name → handler signature. */
export type EventMap = Record<string, (...args: never[]) => void>

/**
 * Minimal strongly-typed event emitter. Universal (no `node:events` dependency),
 * so it works unchanged in the browser, Node, Bun and Deno. Every `.on(...)` in
 * the public API (`Peer`, `P2PKit`, topics, transports) is backed by this.
 */
export class Emitter<E extends EventMap> {
  private readonly handlers: { [K in keyof E]?: Set<E[K]> } = {}

  /** Subscribe to `event`. Returns an unsubscribe function. */
  on<K extends keyof E>(event: K, handler: E[K]): () => void {
    ;(this.handlers[event] ??= new Set<E[K]>()).add(handler)
    return () => this.off(event, handler)
  }

  /** Subscribe once; the handler is removed after its first invocation. */
  once<K extends keyof E>(event: K, handler: E[K]): () => void {
    const wrap = ((...args: Parameters<E[K]>) => {
      this.off(event, wrap)
      ;(handler as (...a: Parameters<E[K]>) => void)(...args)
    }) as E[K]
    return this.on(event, wrap)
  }

  /** Remove a previously registered handler. */
  off<K extends keyof E>(event: K, handler: E[K]): void {
    this.handlers[event]?.delete(handler)
  }

  /** Synchronously invoke every handler registered for `event`. */
  emit<K extends keyof E>(event: K, ...args: Parameters<E[K]>): void {
    const set = this.handlers[event]
    if (!set) return
    // Copy so handlers may unsubscribe (or subscribe) during emission.
    for (const handler of [...set]) (handler as (...a: Parameters<E[K]>) => void)(...args)
  }

  /** Remove all handlers for one event, or all events when `event` is omitted. */
  removeAll<K extends keyof E>(event?: K): void {
    if (event !== undefined) this.handlers[event]?.clear()
    else for (const key of Object.keys(this.handlers)) delete this.handlers[key as K]
  }

  /** Number of handlers registered for `event`. */
  listenerCount<K extends keyof E>(event: K): number {
    return this.handlers[event]?.size ?? 0
  }
}
