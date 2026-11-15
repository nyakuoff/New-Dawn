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

Banding report (Inn doorway "concentric bands", blotchy wall and hat): not a render-chain precision bug. Every post target
after the HDR resolve is RGBA16F (R11G11B10F for bloom mips); only the final display target is RGB8, with the dither added
last in the composite. The reported screenshot contains exactly 256 unique colours in the whole frame (it was
palette-quantized in transit): 17 distinct G levels in the doorway, against 86-91 in a real capture of the same view
(165k unique colours). `showcase/fix-81/inn_banding_histogram.txt`.

## Limits
- Screen-space: lights and contact shadows only see what is on screen. The one or two most important lights also get cube-map shadows (below), the rest keep the contact shadow only.
- The actor mask relies on the character-lighting classifier (per-pixel lighting + Characters on); unlit or classifier-missed actors get only the luminance floor. Enemies were not present in the captured views, so the mask is verified on Link and NPCs only.
- The darkening is a per-class constant, not an adaptive exposure; a few bright interiors may read dim.
- Tatl and other white game lights light their surroundings too (they are point lights).
- Transparent (XLU) surfaces drawn after the opaque-end marker are not lit or darkened.
- Not run: Ikana Castle (no capture point); the Ikana Graveyard at night has no point lights at its start.

## Flicker and character softening (fix #82, 2026-11-14)
- **Root cause of the "lights flicker around characters" report.** The game re-rolls some lights every tick (a torch sets its
  intensity to `128 + rand * 127` at 20 Hz: white noise of up to +-33%). The pass used those values directly, so every surface the
  light reaches strobed, and `LocalFlicker=0` did not help (the cvar is a float: `--cvar ...=0` is stored as an int and read as
  the default; use `0.0`). `RunLocalLights` now low-passes each light's colour and radius (about 0.6 s, wall clock, keyed on the
  light's position in 8-unit cells; a new or returning light starts at its current value) before the designed flicker.
  The designed flicker is also calmer (warm +-12% instead of +-18%, the 5 Hz term weighs 10%).
  South Clock Town torch, 16-shot burst, luminance variation of the scene (std / mean of mean luma): 3.60% -> 1.52%; floor region
  frame-to-frame difference 0.0143 -> 0.0091; the same scene with `Lighting.Local=0` is 0.75%.
- **Characters.** The second (stencil) draw uses a smoothed normal: the 1-pixel depth normal averaged with 3- and 6-pixel
  footprints, and a wider wrap (0.7 instead of 0.3), so a torch rolls over a body instead of snapping between triangles.
- `gNewDawn.Lighting.LocalSmooth` (default 1) turns the filter, the soft normal and the calmer flicker off for A/B comparisons.
- GPU, stage 16, South Clock Town torch: 0.275 -> 0.273 ms.

## Point-light shadows (`gNewDawn.Shadows.PointLights`, roadmap item 4)
`gNewDawn.Shadows.PointLights` (0-2, default 0; **2 in Ultra, 1 in High**), `.PointSize` (64-1024, default 256 per face),
`.PointSoftness` (0-3, default 1, penumbra size), `.PointNear` (default 24, see below), `.PointDebug` (1: the first cube's visibility on its own,
white lit / black shadowed / blue out of range; 2: its stored distance), `.PointDumpDir` (writes the six faces once as raw R32F).
Menu: Advanced -> Lighting & Shadows -> Point-Light Shadows. Needs Local Lights and HDR. Render-only; it only ever removes light.

- **Casters.** The sun-shadow caster recording (world-space opaque triangles, now also including the culled entries of
  `docs/CASCADED_SHADOWS.md` when Coverage is on) is also enabled when `PointLights > 0`, so interiors without a sun get it too.
- **Which lights.** `PrepareLocalLights` (the CPU half of the old `RunLocalLights`, no GL calls) builds the frame's light list; `RunPointShadows`
  picks the best 1-2 by `range / distance to the player`, skipping fairies (white lights Tatl-style, `localBoost < 1`), dim lights, and lights farther
  than range + 500 from the player. A light keeps its slot while it stays among the best (1.5x margin), so two similar torches never swap.
- **Cube map.** Per slot a 256 R32F cube holds the distance from the light to the nearest caster (a fragment writes `length(world - light)`,
  depth-tested, both faces, six faces of the GL cube convention). The cube range is the light's range rounded up to 32 plus 32 (max 800), so
  the smoothed range does not re-render it every frame. The depth clamp the game keeps enabled is turned off for the pass so the near plane
  clips: geometry closer than `PointNear` (24) units to the flame, the brazier rim or lamp shade it sits in, does not cast and so cannot close the
  light in (without it a Great Bay Temple torch lit nothing: a lid 10-17 units above it blocked the whole cube).
- **Only when needed.** The casters are culled to the light's sphere (64-triangle chunk boxes, computed once per frame, then per-triangle boxes) and
  hashed with the light position and range; if nothing near the light changed since the last render the cube is reused (a still scene re-renders
  nothing; a character moving in range re-renders it each frame).
- **Lookup (local-light shader).** `pointShadowVis`: the receiver is offset along its depth normal, a 6-tap blocker search sets the penumbra from
  the blocker distance (contact-hardening: sharp at the occluder, wider away from it, light radius 7 units x `PointSoftness`), then 12 rotated Vogel
  taps filter it with a smooth step. The rotation comes from the per-pixel interleaved noise, so TAA integrates it. The factor multiplies the
  light's contribution next to the existing screen-space contact shadow; the unlit scene is darkened by the same `keep` logic as before, so a
  shadowed wall gets darker, never brighter (#81 moody interiors). Nothing is added to the world shaders.
- **Samplers.** Two dedicated units (34, 35) hold `samplerCube`s with a complete one-texel dummy cube bound from program creation and restored
  after the pass (the Mesa sampler-type rule).

**Evidence** (RX 7800 XT, 1080p, Ultra + community mods, 0 GL errors; `~/dev/ghm-mods/showcase/shadow-coverage/point-shadows/`).
- Light-only view (`gNewDawn.Lighting.LocalDebug=3`): South Clock Town at night, Link beside the torch casts a soft shadow across the floor away from it
  (`sct_ld-off-left-on-right.png`, `crop1to1-sct-...`); Great Bay Temple braziers cast leg and base shadows on the walls and floor
  (`gbt_ld-...`); the Stock Pot Inn wall candle shades the wall below it (`inn_lamp-...`, `crop1to1-inn-lamp-...`). The raw visibility
  (`debug-visibility-*.png`) shows Link's and the braziers' shadows with a penumbra that widens with distance.
- In the normal render the change is real but modest: these lights are small (range 90-325) and the interior grade keeps the unlit scene dark, so
  the cube shadows read as deeper contact and cast shadows near a flame rather than a new look. Frame mean luminance moves by under 1.5 gray levels
  (Inn lamp 24.5 -> 24.3, Woodfall 28.4 -> 27.4, Great Bay Temple 53.6 -> 54.8).
- GPU: cube stage + local-light stage change, 300-frame averages. Stock Pot Inn lamp (1 light) 0.047 + 0.028 = **+0.075 ms**; Great Bay Temple
  (2 torches) 0.075 + 0.175 = **+0.25 ms**; South Clock Town torch (2) 0.112 + 0.212 = **+0.32 ms**. Together with Coverage (<= 0.014 ms GPU)
  the total stays under 0.35 ms on Ultra, inside the 1 ms budget. Render-thread CPU for the culling and cube submission 0.10-0.38 ms per frame.
- Off identity: `PointLights=0` and `Coverage=0` leave the frozen Stock Pot Inn byte-identical to main (SHA-256 `09f407ea2cecf510`, see
  `docs/CASCADED_SHADOWS.md`).

**Limits.** Opaque casters only (cut-out foliage and alpha-blended geometry do not shadow a flame); at most two lights, none for fairies; a
light attached to a fixture larger than `PointNear` units sees that fixture as a caster; very bright interiors show little change because the added
light is a small share of the lit result.
