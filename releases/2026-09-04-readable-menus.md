# DuneCity 1.0.532 / Android 0.2.21

## Readable Start Menus

- Rebuild the main and single-player menus with generously sized, spaced
  buttons. Preserve the original gold controls, Arrakis artwork and Dune theme.
- Use dark, centered button lettering with font sizes that fit each control.
  Keyboard focus retains a visible outline; explicit custom text colors remain.
- Add DISPLAY to the main menu, before starting a game. Choose Large
  (640x480), Medium (800x600), Small (1024x768), or desktop Automatic.
- Save the chosen interface resolution across restarts. Android uses Large by
  default, independently of the phone's native display resolution, and exposes
  the same presets in Options. Keep landscape and fold/unfold surface handling.
- Shorten and wrap the first-launch DuneCity prompt to fit small screens.
- Split Options into General, Display, and Audio / Network tabs with stable
  row heights and always-visible Back / Apply controls. Keep settings edits
  pending across tabs until Apply, with Back discarding them.

## Dune2R Assets

- Include the verified Refinery footprint scaling and alignment corrections.
  The current remastered Refinery is bound to House Atreides only.
- Include downloadable atlas refresh and replacement support. Use Dune2R
  EditoR > ASSETS > REFRESH, then download the desired visual pack.
- Preserve installed downloads and local rollback revisions during mod updates.
  A failed or offline catalog refresh preserves the previous catalog.
- Validate asset geometry, visual file types, size limits and official immutable
  download URLs before installing. New supported visual packs can be deployed
  without rebuilding the game.

The Android APK excludes optional remastered atlases; they download in-game.
These changes do not alter gameplay rules, unit balance, QuantBot or multiplayer
simulation. The private QBot simulator is not part of the game packages.
