# Modern rain, snow and wet surfaces (A2)

`gNewDawn.Atmos.Weather` (default **0**, **1 in the New Dawn preset**; menu toggle "Modern Rain, Snow and Wet Surfaces (HDR)").
Needs HDR. Reads `envCtx.precipitation` only; native rain updates and RNG still run. When the replacement is
ready, only the native rain *draw* is suppressed through begin/end markers in `z_kankyo.c`
(`NewDawnHDR_WeatherScope`); on any failure (roof FBO, shader, sprite) native rain is kept.

- Roof shelter: a 1024² orthographic overhead depth map of the recorded opaque geometry (huge triangles such as
  sky domes are excluded) around the camera. Rain, splashes and wetness are discarded under it.
- Wet layer (before the water reflection copy): darkened albedo, sky-cube reflections on flat puddle patches,
  sun specular, CC0 ring sprites as splashes/ripples. Wetness lingers about 30 s after rain stops and resets on
  scene change.
- Rain: 20 000 instanced, camera-wrapped streaks with pixel-space minimum width and motion blur along the
  projected fall vector, sky-tinted, depth-softened, roof-occluded. Snow: 8 000 soft flakes with drift.
- Assets: `splash_64.png` (ring) and the A1 dust sprite for streak/flake softness; see `libultraship/assets/atmos/MANIFEST.md`.

Evidence (2026-11-13, Mesa RX 7800 XT, 1080p, MESA_DEBUG=context, 0 GL errors): day-2 rain South Clock Town
(streets sheltered under awnings, dry under roofs, ripples), Mountain Village snow. Splits:
`~/dev/ghm-mods/showcase/atmos/a2/`. GPU (stage 12 wet+roof / 13 precipitation): rain 0.12–0.14 / 0.02–0.05 ms,
snow 0.010 / 0.010 ms.

Limits: roof map is vertical (rain slant ignored) and single-layer; streak/flake colour is a heuristic from the
fog colour; snow coverage remains limited to Mountain Village. Exact-main byte identity, night rain and lingering
wetness are now verified as described below.

Merged onto main 99bdb022d / renderer 4deaa8c2: the wet layer skips pixels under any water-plane box (the water pass owns foam and ripples there), so splashes/puddles are not doubled on water. Verified in Clock Town rain with Water.Foam/Refraction/Enabled, Wind and Particles all on: 0 GL errors; stage 12 0.17 ms, 13 0.03 ms.

## Integration update, 2026-11-14

Rain and snow travel now use the integrated shared wind; rain streak tilt uses its instantaneous
velocity. All shipped weather code still reads native precipitation without writing it.

New captures, RX 7800 XT / Mesa OpenGL, 1080p, MSAA 4, preset and full mods:

- **Night rain:** Clock Town at 22:00, day 2, native precipitation=60 injected by the private
  disposable fixture (the normal day-2 weather schedule ends at 17:30). Soft sky-tinted streaks,
  rings and wet ground are visible. This validates controlled nighttime rendering, not a change
  to the native weather schedule. Three-frame burst inspected.
- **Rain stops:** the private fixture sets precipitation to zero at launch +15 seconds, after
  wet ground is established. Captures at +12/18/24/30/36/42/48 seconds show rain, lingering
  reflections after rain stops, then dry ground at +33 seconds after the stop. Unscaled ground
  crops compare active rain / +3 seconds / +33 seconds. The game logic/camera are frozen; sky
  and renderer animation continue.
- **Off identity:** the same exact-main frozen-Inn check as `ATMOS_WIND.md` passes in four
  captures (two main, two atmosphere), with all three atmosphere toggles off.
- GPU in night rain, 300-run means: **0.168 ms** wet/roof (stage 12), **0.031 ms** precipitation
  (stage 13), **0.006 ms** ambient particles (stage 11). Wind displacement **0.0105 ms CPU/frame**
  including debug timer overhead. Snow's stages 12/13 each measure **0.011 ms GPU** in the
  final Mountain Village check. These are stage costs, not whole-frame performance claims.

Evidence: `~/dev/ghm-mods/showcase/atmos/final/{night-rain*,raining.png,rain-stopped-*,rain-stop-*,wetness-*}`.
Reproduction commands and raw logs: `~/dev/ghm-mods/work/atmos/final-{night-rain,wet-linger}/` and
matching `.command.json` files. No new assets: existing CC0 dust, leaf and ring sprites are reused.
