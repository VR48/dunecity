/**
 * A peer's identity on the mesh. With a {@link Signer} set this is a proven
 * cryptographic address (e.g. an Ethereum-style `0x…` address from
 * `ECDSASigner`); otherwise it is any caller-supplied opaque string.
 */
export type PeerId = string
