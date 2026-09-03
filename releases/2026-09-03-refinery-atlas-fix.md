# Atreides Refinery Atlas Fix

- ASUS testing found the Atreides pack correctly registered, but building PNG
  dimensions disagreed with their manifest. The shared atlas writer used frame
  width for row height, clipping/overlapping rectangular Refinery frames.
- Packager now preserves rectangular cells, uses 2048px maximum building pages,
  and includes an independent fallback still for every building state.
- Native animation-page decoding is asynchronous, with next-page prefetch and
  a 192 MiB building texture cache. Invalid PNG geometry is rejected from the
  header before decoding; failed pages are not repeatedly retried until reload.
- Sprite fallback priority remains animation, enhanced still, then classic.
  Atreides-only binding, Gravel topology and the visual crossfade are unchanged.
- Replaces the initial malformed Refinery payload. Install the new executable
  and select Atreides Refinery Remastered > DOWNLOAD to update existing assets.
- Verification covers rectangular frame round trips, actual PNG geometry,
  all seven states, cache reload/eviction, malformed files, and native GPU
  playback with changing pixel output and no misses on the warmed second loop.
- Android test version 0.2.18-refinery-test (1000538) includes the new renderer
  and catalog, with remastered packs still downloaded separately. Packaging
  explicitly strips staged native libraries using the selected NDK, retaining
  the original native build's debug symbols locally for crash diagnostics.

## Reproduce Checks

```powershell
py -3 -m unittest discover -s tests -p test_dune2r_packaging.py -v
$env:DUNE2R_ATLAS_TEST_ROOT = '<path to packaged refinery>'
$env:DUNE2R_GPU_TEST = '1'
$env:SDL_RENDER_DRIVER = 'direct3d11'
# Optional SDL screenshot, produced by the regression test itself:
$env:DUNE2R_ATLAS_CAPTURE = '<absolute output path>.bmp'
ctest --test-dir <build directory> --output-on-failure
```
