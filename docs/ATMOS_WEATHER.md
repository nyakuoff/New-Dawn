# Modern rain, snow and wet surfaces (A2, experimental)

`gNewDawn.Atmos.Weather` (default **0**, not in the preset; menu toggle "Modern Rain, Snow and Wet Surfaces (HDR)").
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
fog colour; snow is only checked in one Mountain Village view; exact-main byte identity of the off path is not
proven (same open fixture gate as A1); lingering wetness and heavy-rain night scenes not captured.

Merged onto main 99bdb022d / renderer 4deaa8c2: the wet layer skips pixels under any water-plane box (the water pass owns foam and ripples there), so splashes/puddles are not doubled on water. Verified in Clock Town rain with Water.Foam/Refraction/Enabled, Wind and Particles all on: 0 GL errors; stage 12 0.17 ms, 13 0.03 ms.
