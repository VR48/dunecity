/**
 * A WebRTC implementation, normalized to the constructors P2PKit uses. Browsers
 * provide these natively; Node uses `@roamhq/wrtc`, Deno uses `werift`
 * (README §10).
 */
export interface RTCBackend {
  RTCPeerConnection: new (config?: RTCConfiguration) => RTCPeerConnection
  RTCSessionDescription?: typeof RTCSessionDescription
  RTCIceCandidate?: typeof RTCIceCandidate
}

/** Anything that structurally provides at least an `RTCPeerConnection` ctor. */
export type RTCBackendSource = { RTCPeerConnection: unknown } & Record<string, unknown>

function normalize(source: RTCBackendSource): RTCBackend {
  return {
    RTCPeerConnection: source["RTCPeerConnection"] as RTCBackend["RTCPeerConnection"],
    RTCSessionDescription: source["RTCSessionDescription"] as RTCBackend["RTCSessionDescription"],
    RTCIceCandidate: source["RTCIceCandidate"] as RTCBackend["RTCIceCandidate"],
  }
}

async function tryImport(name: string): Promise<RTCBackend | undefined> {
  try {
    const mod = (await import(/* @vite-ignore */ name)) as Record<string, unknown>
    const source = (mod["default"] ?? mod) as RTCBackendSource
    if (source && typeof source["RTCPeerConnection"] === "function") return normalize(source)
  } catch {
    /* not installed in this runtime */
  }
  return undefined
}

/**
 * Resolve the runtime's WebRTC backend. Detection order: explicit `override` →
 * native globals (browser) → `@roamhq/wrtc` (Node) → `werift` (Deno). Pass
 * `override` (e.g. `getRTC(werift)`) only to force a specific implementation.
 */
export async function getRTC(override?: RTCBackendSource): Promise<RTCBackend> {
  if (override && typeof override["RTCPeerConnection"] === "function") return normalize(override)

  if (typeof globalThis.RTCPeerConnection === "function") {
    return {
      RTCPeerConnection: globalThis.RTCPeerConnection,
      RTCSessionDescription: globalThis.RTCSessionDescription,
      RTCIceCandidate: globalThis.RTCIceCandidate,
    }
  }

  const wrtc = await tryImport("@roamhq/wrtc")
  if (wrtc) return wrtc

  const werift = await tryImport("werift")
  if (werift) return werift

  throw new Error(
    "No WebRTC backend found. Install @roamhq/wrtc (Node) or werift (Deno), or pass one via getRTC() or the backend option.",
  )
}
