/**
 * Public STUN servers used by WebRTC when the caller supplies no `iceServers`.
 * STUN only discovers a peer's public address for hole-punching; it does NOT
 * relay. Symmetric NATs and locked-down networks still need a TURN relay, which
 * is bring-your-own (see README §9).
 */
export const DEFAULT_ICE_SERVERS: RTCIceServer[] = [
  { urls: "stun:stun.l.google.com:19302" },
  { urls: "stun:stun1.l.google.com:19302" },
  { urls: "stun:stun2.l.google.com:19302" },
  { urls: "stun:global.stun.twilio.com:3478" },
]
