# Refinery Footprint Scaling Fix

- Fix enhanced building drawing that treated 64 world units per tile as screen
  pixels. A tile is 16 pixels at base zoom, so the old rendering was four times
  too wide and tall at every zoom level.
- Use screen-pixel footprint sizing for both animation frames and still
  fallbacks. Preserve aspect ratio and the bottom-center ground anchor.
- Keep terrain visible below transparent buildings while Dune2R visuals are
  enabled, preventing black footprint rectangles. Classic mode is unchanged.
- Leave terrain scale, units, house bindings, gameplay, and downloadable asset
  data unchanged. The corrected Refinery pack does not need republishing.
- Add regression checks against the engine's world-to-screen conversion at
  every supported zoom level, different source resolutions, and custom anchors.
- Exercise the same sizing helper in native GPU playback against a tile grid
  and the building's ground-footprint outline.
- Prepare Android 0.2.19-scale-test (1000539) and a Windows scale-test build.

Install the new game build. Keep the corrected 264 MiB Refinery pack from the
previous test; this fix does not require downloading it again. A 3-tile-wide
Refinery should render 48, 96, or 144 logical pixels wide at zoom levels
0, 1, or 2. Display scaling may enlarge the whole viewport uniformly.
