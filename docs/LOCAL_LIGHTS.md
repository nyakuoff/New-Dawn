# Dynamic local lighting and night contrast

Render-only, HDR only. `gNewDawn.Lighting.Local` defaults to **0** and is **1 in the New Dawn preset**.
Other cvars: `.LocalStrength` (0–3, 1), `.LocalContrast` (0–1.5, 1; 0 only adds light), `.LocalFlicker` (0–1, 1),
`.LocalDebug` (N: show only the light contribution × N and log the light list).

## How it works
- `NewDawnFillLocalLightParams` (NewDawnPostFx.cpp) snapshots the game's own point lights (`LightContext`, glow and
  no-glow, nearest 16 by distance over radius) and the player position into the per-frame params. Nothing is written back.
  It also derives a darkening amount from the game clock and light mode, read-only: outdoors (`LIGHT_MODE_TIME`) a smooth
  dusk/dawn ramp to 0.62 at night; interiors and dungeons (`LIGHT_MODE_SETTINGS`) a constant 0.35; zero in cutscenes.
- `PostFxGL::RunLocalLights` runs at the opaque-end marker after AO and GI. It appends the VFX flames recorded last frame
  that have no game light within 90 units (up to 8, warm white-orange, range 195), applies the flicker, and draws one
  full-screen pass. No game shader is edited, so vertex-coloured rooms and actors are lit alike and the packaged
  `default.shader.glsl` is untouched.
- Per pixel (depth + depth-derived normal): softened inverse-square falloff with a smooth range window (range = game
  radius × 1.3), wrapped Lambert, an 8-step screen-space contact shadow toward the light, a per-channel soft cap, and the
  scene colour's square root as the albedo proxy. The unlit scene is multiplied down by the darkening (cool, dim moon tint,
  a 0.009 luminance floor) and surfaces the lights reach keep their authored
  brightness. Characters (Link, enemies, NPCs, props: the draws the character-lighting classifier flags) write stencil 1 while the
  frame is drawn, and a second draw darkens them by only 40% of the world value. Dual-source blending (`GL_ONE, GL_SRC1_COLOR`) keeps MSAA samples intact.
- Flicker: three sines (≈1.3, 2.9, 5.1 Hz) keyed on the light's 4-unit-quantised position and wall-clock time; warm lights
  swing up to ±18%, neutral lamps ±3%. It never touches game data.
- No new sampler: the pass reuses the existing colour (unit 8) and depth (unit 9) lanes bound to dummies at program creation.

## Evidence (2026-11-14, RX 7800 XT, 1080p, MSAA 4, preset + full mods; `~/dev/ghm-mods/showcase/lights/`)
- **Off = main.** Frozen Stock Pot Inn (clock 12, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`, GI and wind off
  because their jitter is wall-clock keyed): main build (`17a6cedf4` sources) and this branch with Local=0 have the same
  PNG SHA-256 `247b7072…`; Local=1 differs (`3a52655f…`, before the contrast work). 0 GL errors under `MESA_DEBUG=context`. Re-checked after merging main `cae7c2b6b` / new-dawn `6c3edb2e` (POM): Local=0 still `247b7072…` in two runs, Local=1 `97c1e82a…`, 0 GL errors. That comparison is against the earlier main build; a fresh build of the POM main was not made.
- **GPU** (`PostFx.Debug.Stats`, stage 16, 300-run average, two stencil-split draws): 0.17 ms Inn, 0.24 ms South Clock Town, 0.31 ms Great Bay Temple.
- **Luminance, round 2** (linear, scene area without HUD; mean / p5 / p95, off → on):

  | view | off | on | mean on/off |
  | --- | --- | --- | --- |
  | South Clock Town, torch close (00:00) | 0.0828 / 0.0050 / 0.1907 | 0.1000 / 0.0092 / 0.3086 | 1.21 |
  | South Clock Town, wide (00:00) | 0.0565 / 0.0062 / 0.1498 | 0.0289 / 0.0072 / 0.0837 | 0.51 |
  | Termina Field (00:00) | 0.1426 / 0.0047 / 0.5513 | 0.0553 / 0.0047 / 0.1816 | 0.39 |
  | Stock Pot Inn, lamp | 0.0330 / 0.0041 / 0.1177 | 0.0155 / 0.0042 / 0.0417 | 0.47 |
  | Stock Pot Inn, wide | 0.0773 / 0.0051 / 0.3672 | 0.0347 / 0.0050 / 0.1229 | 0.45 |
  | Shop interior | 0.0650 / 0.0073 / 0.1967 | 0.0236 / 0.0069 / 0.0672 | 0.36 |
  | Dark house interior | 0.0205 / 0.0053 / 0.0715 | 0.0123 / 0.0053 / 0.0240 | 0.60 |
  | Woodfall Temple (00:00) | 0.0498 / 0.0081 / 0.2334 | 0.0479 / 0.0081 / 0.2495 | 0.96 |
  | Great Bay Temple (00:00) | 0.0876 / 0.0071 / 0.4188 | 0.1162 / 0.0196 / 0.3261 | 1.33 |

  Round 1 (interior darkening 0.35) left the Inn at 0.79; round 2 (0.70, lamp gain 1.5) brings it to 0.45-0.47 while the lamp
  pool keeps its brightness. Only the Inn lamp view was recaptured after the final contact-shadow softening.
- **Flicker burst** (16 shots, South Clock Town torch pool): ±7% around the mean, largest step 10.7% — a slow drift, no strobing.
  The measured region includes the native flame sprite.

## Dark-area fix (#81, 2026-11-14)
Playtest: dark scenes and the opening looked grey, lifted, miscoloured and "oversharp". Bisected with per-feature cvars:
- Darkening was **zero in every cutscene** (the opening is mostly cutscenes), so Lost Woods, the tunnel and the Clock Tower
  interior kept the flat authored light with 4K textures at full crispness. Cutscenes now get 85% of the normal value, eased
  over about a second (`sDarken` in `NewDawnFillLocalLightParams`) so a cutscene start or end never pops.
- The luminance floor (0.009 linear, about 25/255) pinned every dark pixel, so p5 sat at 0.004-0.008 in every dark scene
  whatever else was on. Now 0.0055; the interior darkening is 0.58 (was 0.70).
- The darkening multiplied fogged distance too, giving a black ground band against the undarkened sky (opening forest). It now
  fades with the native fog factor.
- The art grade added a fixed lift, a gamma below 1 and a blue/violet shadow tint to true blacks. They now fade in above the
  deepest shadows (`dk` mask, tint mask), so blacks stay black and unshifted.
- Woodfall Temple volumetric fog density 1.6 to 0.5 (a teal veil over the whole temple).
- Preset exposure +0.3 to +0.05 EV (about -0.25 EV; daytime Termina mean -9%).
- Tatl and other fairies light with pure white (255,255,255): now tinted by the fairy colour Tatl is showing, range x0.75 and
  `localBoost` 0.3 (was white rgb 4.04 at range 130; now rgb about 1.2/0.9/0.8 at range 98).
Materials, parallax, sky reflections, AO and GI were toggled and make no visible difference in the darks (1:1 crops); the
crispness came from the missing darkening plus the lift, not from a sharpen step (there is none). Luminance (mean / p5 / p50 /
p95, before to after): Lost Woods .0142/.0041/.0090/.0298 to .0144/.0029/.0091/.0353; tunnel .0256/.0074/.0103/.1034 to
.0199/.0040/.0109/.0531; Clock Town 00:00 .0373/.0077/.0170/.1146 to .0304/.0039/.0134/.0903; Inn .0469/.0059/.0135/.1883 to
.0442/.0039/.0148/.1633; Woodfall .0569/.0082/.0138/.2829 to .0322/.0035/.0060/.1391; Termina 12:00 (guard)
.2249/.0139/.1068/.7537 to .2041/.0115/.1072/.7156. Images: `~/dev/ghm-mods/showcase/fix-81/`.

## Limits
- Screen-space: lights and contact shadows only see what is on screen; no cube shadow maps (stretch goal not done).
- The actor mask relies on the character-lighting classifier (per-pixel lighting + Characters on); unlit or classifier-missed actors get only the luminance floor. Enemies were not present in the captured views, so the mask is verified on Link and NPCs only.
- The darkening is a per-class constant, not an adaptive exposure; a few bright interiors may read dim.
- Tatl and other white game lights light their surroundings too (they are point lights).
- Transparent (XLU) surfaces drawn after the opaque-end marker are not lit or darkened.
- Not run: Ikana Castle (no capture point); the Ikana Graveyard at night has no point lights at its start.
