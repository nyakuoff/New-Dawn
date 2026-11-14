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
  that have no game light within 90 units (up to 8, warm orange, range 260), applies the flicker, and draws one
  full-screen pass. No game shader is edited, so vertex-coloured rooms and actors are lit alike and the packaged
  `default.shader.glsl` is untouched.
- Per pixel (depth + depth-derived normal): softened inverse-square falloff with a smooth range window (range = game
  radius × 1.3), wrapped Lambert, an 8-step screen-space contact shadow toward the light, a per-channel soft cap, and the
  scene colour's square root as the albedo proxy. The unlit scene is multiplied down by the darkening (cool, dim moon tint,
  a 0.009 luminance floor, a soft exemption around the player) and surfaces the lights reach keep their authored
  brightness. Dual-source blending (`GL_ONE, GL_SRC1_COLOR`) keeps MSAA samples intact.
- Flicker: three sines (≈1.3, 2.9, 5.1 Hz) keyed on the light's 4-unit-quantised position and wall-clock time; warm lights
  swing up to ±18%, neutral lamps ±3%. It never touches game data.
- No new sampler: the pass reuses the existing colour (unit 8) and depth (unit 9) lanes bound to dummies at program creation.

## Evidence (2026-11-14, RX 7800 XT, 1080p, MSAA 4, preset + full mods; `~/dev/ghm-mods/showcase/lights/`)
- **Off = main.** Frozen Stock Pot Inn (clock 12, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`, GI and wind off
  because their jitter is wall-clock keyed): main build (`17a6cedf4` sources) and this branch with Local=0 have the same
  PNG SHA-256 `247b7072…`; Local=1 differs (`3a52655f…`, before the contrast work). 0 GL errors under `MESA_DEBUG=context`.
- **GPU** (`PostFx.Debug.Stats`, stage 16, 300-run average): 0.13 ms Inn, 0.21 ms South Clock Town, 0.29 ms Great Bay Temple.
- **Luminance** (linear, scene area without HUD; mean / p5 / p95, off → on):
  | view (clock) | off: mean / p5 / p95 | on: mean / p5 / p95 | mean on/off |
  | --- | --- | --- | --- |
  | South Clock Town, torch close (00:00) | 0.0889 / 0.0051 / 0.2089 | 0.1055 / 0.0092 / 0.3125 | 1.19 |
  | South Clock Town, wide (00:00) | 0.0567 / 0.0062 / 0.1506 | 0.0301 / 0.0072 / 0.0809 | 0.53 |
  | Termina Field (00:00) | 0.1426 / 0.0047 / 0.5513 | 0.0641 / 0.0047 / 0.2260 | 0.45 |
  | Ikana Canyon (00:00) | 0.1739 / 0.0338 / 0.3196 | 0.0819 / 0.0198 / 0.1913 | 0.47 |
  | Stock Pot Inn, lamp (12:00) | 0.0330 / 0.0041 / 0.1177 | 0.0260 / 0.0042 / 0.0872 | 0.79 |
  | Stock Pot Inn, wide (12:00) | 0.0773 / 0.0051 / 0.3672 | 0.0613 / 0.0050 / 0.3016 | 0.79 |
  | Woodfall Temple (00:00) | 0.0498 / 0.0081 / 0.2333 | 0.0493 / 0.0081 / 0.2461 | 0.99 |
  | Great Bay Temple (00:00) | 0.0831 / 0.0071 / 0.4091 | 0.1092 / 0.0187 / 0.3605 | 1.31 |
  | South Clock Town torch, dusk (18:30) | 0.0500 / 0.0049 / 0.2126 | 0.0730 / 0.0072 / 0.2935 | 1.46 |

  Night outdoors falls to 0.45-0.53 of the mean; the p5 floor holds (never below ~0.005). Torch-lit pools rise instead
  (close torch +19% mean, +50% p95). Temples stay near their original key (lights offset the darkening), and dusk gets
  brighter because the torches add to a still-bright scene.
- **Flicker burst** (16 shots, South Clock Town torch pool): ±7% around the mean, largest step 10.7% — a slow drift, no strobing.
  The measured region includes the native flame sprite.

## Limits
- Screen-space: lights and contact shadows only see what is on screen; no cube shadow maps (stretch goal not done).
- The player exemption is a world-space ellipsoid, so enemies and props get only the luminance floor, not an exemption.
- The darkening is a per-class constant, not an adaptive exposure; a few bright interiors may read dim.
- Tatl and other white game lights light their surroundings too (they are point lights).
- Transparent (XLU) surfaces drawn after the opaque-end marker are not lit or darkened.
- Not run: Ikana Castle (no capture point); the Ikana Graveyard at night has no point lights at its start.
