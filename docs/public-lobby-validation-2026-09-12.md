# Public lobby and support ownership validation — 12 September 2026

Local branch: `fix/network-hardening`. No push, merge or public website deployment performed.

## Changes

- `6a82c65`: centralized relay-backed compatible public-room directory, player names, private hosting.
- `947a65c`: confirmed-name shared lobby chat, host-only visibility changes, private-host-only codes,
  separate polling budget, and Hermes' UTF-8 directory fix.
- `5ad37fe`: addressed Hermes chat review: all-route ingress limits; removed accidental random
  token creation on AdmissionError; bounded per-address reservations and immutable session
  lifetime; independent bounded channel histories and explicit history-gap reporting.
- `3c9ff56`: user-reported support ownership fix. Normal host controls no longer change a
  guest's support AI. Client requests are checked on the host, dropdown callbacks recheck
  pre-edit ownership, and separate click-to-claim handlers cannot steal another human/support.
- `4699593`: Hermes found inactive odd seats still counted as owners; ownership checks now ignore
  those seats when multiple players per house is disabled. Regression test passes.

## Verification

- Node relay suite: 184/184, 20 suites; `lobby-chat-node-final-tests.log`.
- Wasm32 parser harness: 170 checks, zero failures; `lobby-chat-reviewed-wasm-tests.log`.
- Native relay wire/session CTest: 2/2; `lobby-chat-reviewed-ctest.log`.
- Native ownership CTest after the final inactive-seat fix: passes;
  `support-ai-final-tests.log`. Native and web builds pass; dependency checks pass before/after.
- Two actual browser clients at localhost:8766, relay localhost:8787: explicit name confirmation,
  Alice/Bob shared chat both ways; public-to-private removes listing and shows host code;
  private-to-public restores listing and hides code; public row joins without entering a code;
  map lobby contains both players with no code in caption/chat.
- Support ownership GUI tested on `3c9ff56`: Alice selected QuantBot Easy, Bob selected QuantBot
  Medium; both views synchronized. Opponent dropdown arrows and bodies could neither open AI
  choices nor move a player. Both test clients remain in that public map lobby. The additional
  inactive-seat fix `4699593` is compiled and packaged for subsequent reloads, and is covered by
  native regression tests; it does not change this active two-seat-per-house scenario.

Actual Hermes reviews (read-only): public directory session `20260912_095921_bea1a3`, chat
`20260912_101437_72d731`, chat fix verification (184 tests, no remaining findings in that delta),
and support ownership `20260912_103042_688825`. The last review found the inactive-seat issue;
Codex fixed it and ran the new regression. Reports and build logs are under
`/Users/stefan/Documents/projects/outputs/network-hardening/`.

Claude's earlier quota block is not a completed review of this new code. Native/browser actual
multiplayer gameplay remains a separate uncompleted release check. This work verified browser
lobby flows and ownership, not a fresh full-match soak or production WSS deployment. The game
host remains authoritative for game snapshots; this is not a defense against a modified hostile
host. Chat names are temporary display names, not accounts. Legacy PHP list/list2 remain unchanged;
public relay discovery/chat are in-memory, separate from SQLite runtime analytics.
