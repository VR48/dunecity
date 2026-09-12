# Crossplay final review — 12 September 2026

Codex completed its review and reconciled the actual Hermes review against the source. No
blocking code defect remains identified by those reviews. Claude's requested final review did
not run: its CLI returned HTTP 429 before reading any input, with a session reset at 11:50 UTC
(21:50 Sydney) on 12 September. This record is not public deployment or release approval.

## Reviewed source and scope

- Game: snapshot `ac122ec2bf0f6bbca605f40a1ad0c08cb27a4f72`, followed by review fixes
  `0dda5c4` and `f5b496b` on `fix/network-hardening`.
- Website receiver: `b1fe1b8` on `fix/relay-analytics` in the separate website worktree.
- Actual Hermes session: `20260912_085125_94fee6`. The remote files were exported with
  `git archive`; there was no remote Git metadata. Hermes reported no blocking defect in the
  base snapshot and no blocker or unresolved regression in the final combined fix patch.
- Actual Claude attempt: `ba7d61c9-4f45-4045-9fe4-0cded579d3e8`, zero input/output tokens.
  Earlier implementation work by Claude does not constitute this final independent review.

The review covered admission/grant consistency, room and player authorization, message routing,
queue/rate/size/time bounds, callbacks and disconnect lifetime, content/start checks, relay map
handling, legacy ENet boundaries, pause/digests, CSP, and authenticated additive SQLite analytics.
Browser/native runtime fields are client claims, not attested platform identity.

## Findings resolved

1. Protocol/content handshake refusals lacked a diagnostic event. Both refusal paths now emit
   the numeric denial code and salted address tag through the existing log allowlist. Tests
   assert the exact safe keys; grants, fingerprints and free-form reasons are not added.
2. The bulk fixture allowed more messages than its 256 unique byte patterns. Both count options
   now cap at 256 (indices 0–255). The receiver also rejects duplicate, missing, out-of-order or
   out-of-range indices, instead of accepting the correct number of intact but repeated frames.
   Usage and script comments describe the pacing and partial-write limitation accurately.

Older Hermes claims were checked rather than accepted automatically. Unsupported claims about
regex character acceptance, log encryption, peer-ID reuse, form parsing and MOD_ACK identity
are not findings of the final review.

## Verification

| Check | Result and limit |
| --- | --- |
| Relay suite after final code changes | 174 tests pass, 16 suites |
| Native harness rebuild | Pass; dependency records checked before and after |
| Native bulk transfer after sequence fix | 48 × 200,000-byte bodies arrive intact and in order |
| Negative bulk delivery | Sequences `0,0` and `1,0` fail with an unexpected-index error |
| Count-option bounds | Both bulk options reject 257 before connecting |
| CSP packager | 3 tests pass |
| SQLite receiver | 5 Python tests; PHP authentication and PDO/Python storage fixtures pass |
| Earlier native/wasm checks | Four CTest targets and 140 wasm wire checks pass; agreement, injected divergence and real Game command/pause probes pass |
| Browser early join | On build 073e315, guest joins before map selection; correct seats on both clients; both enter gameplay |
| Browser menu regression | On build 8a793d6, menu open for 71 seconds; both games continue; sampled digests match; movement works after closing |
| Analytics delivery | Seven signed fixture events delivered from Node through PHP to isolated SQLite |

The browser checks used two in-app browser clients over loopback. Sampled 28-byte state digests
are useful evidence, not a proof of full simulation determinism. The latest source fixes affect
relay diagnostics and the test harness; they do not change the browser game executable.

Local review reports, test output and the failed Claude attempt are preserved under
`../outputs/network-hardening/` relative to this worktree. In particular:
`hermes-final-snapshot-review.txt`, `hermes-delta-review.txt`, `hermes-final-delta-review.txt`,
`claude-final-review-attempt.json`, `final-review-node-tests.log`,
`relay-transport-bulk-final-review.log`, `bulk-order-negative.log`, and
`browser-menu-retest.json`, and `browser-early-join-review.json`.

## Remaining release gates

- Obtain Claude's final review when its existing account limit resets.
- Play an actual native-to-browser match, including commands, menus, disconnects and digests.
- Exercise native libcurl partial socket writes deliberately. The successful bulk fixture did
  not force that path; a queue backlog alone would not prove partial writes either.
- Verify public WSS certificates, reverse-proxy settings, Origin/address forwarding and
  browser connectivity across real networks, latency and loss.

No public deployment, release, merge or push was performed as part of this review. PR 24
(`aaebeea2f0136b429001fe3dcd2a96b66f009f61`) and Quix PR 3
(`db4c81ec5bf132d4bd18f873af08bec04bd87b6e`) were unchanged when checked during this review.
