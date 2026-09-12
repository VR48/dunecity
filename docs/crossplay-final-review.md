# Crossplay final review — 12 September 2026

## 1.0.657 candidate checkpoint

The earlier review below is historical. Actual Claude Opus subsequently completed
reviews in sessions `ef3ab597-7d61-46c6-b462-59e2c024381c` and
`c1aecb58-80b0-4eeb-b8a7-2361c34861e2`. The second checked snapshot `4940aef` and ran
185 Node tests successfully. The release candidate incorporates current main `8879732`.

Resolved findings include rotating public room codes when making a room private,
invalidating unused old admissions, recovering visibility requests by host control token,
counting the response newline in its byte budget, requiring explicit production origins
and proxy trust, enabling curl WebSockets in vcpkg, and checking packaged binaries for
secure relay support. Follow-up changes wait explicitly for the Windows GUI executable,
reject duplicate response room codes and explain an unconfirmed visibility change.
The directory name encoding concern was checked: binary HELLO strings are preserved as
Latin-1 byte containers, so converting those strings back to Latin-1 bytes for the
hex directory response preserves the original UTF-8 bytes; changing that boundary alone
would double-encode names. Token-first visibility authorization deliberately supports
retries with old invitation codes; the control token is the authority.

Local checks: all five CTest targets pass, Node 185/185, wasm32 wire parser 173/173,
web packager 3/3, shell 5/5. The test-only curl wrapper exercises the unchanged production
send queue with short writes and injected `CURLE_AGAIN`; eight 200,000-byte messages
arrive intact and in order. This is deliberate API backpressure, not a claim that an
OS socket naturally saturated. Native dependency audits pass and the local desktop
binary reports secure relay support.

Stefan completed an earlier two-browser match (about 20 minutes) on `3c9ff56`.
Sampled logs showed no digest mismatch or premature disconnect; digests at cycles
18,200 and 18,400 agreed. Average FPS fell from roughly 145–150 to 104.9, with later
rolling samples near 92; the cause remains unproven and is a follow-up, not a fixed issue.
A subsequent 1.0.657 browser-host/native-client match joined through the public list
without an invitation code. Actual digests agree at cycles 8,400, 8,600 and 8,800.
That live test remains in progress; later lobby-only fixes are not loaded into it.

Actual Hermes reviewed the production bootstrap and identified activation/retry,
artifact identity and rollback gaps. That installer is being revised and has not been
executed. The restricted metaserver account cannot administer services; administrator
access remains unknown. No production relay, main merge or stable tag is claimed.
Candidate package CI: https://github.com/VR48/dunecity/actions/runs/34693670790
(the `4940aef` candidate, before the follow-up lobby presentation changes).

## Earlier reviewed source and scope

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

- Complete native/browser commands, menus and disconnect checks and verify the revised lobby layout.
- Complete review and verification of the revised administrator bootstrap.
- Verify public WSS certificates, reverse-proxy settings, Origin/address forwarding and
  browser connectivity across real networks, latency and loss.

No public deployment, release, merge or push was performed as part of this review. PR 24
(`aaebeea2f0136b429001fe3dcd2a96b66f009f61`) and Quix PR 3
(`db4c81ec5bf132d4bd18f873af08bec04bd87b6e`) were unchanged when checked during this review.
