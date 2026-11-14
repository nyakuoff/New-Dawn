# HD VFX replacement (fire, glows, embers, dust, sparks)

Render-only OpenGL feature, HDR required. `gNewDawn.Vfx.Enabled` defaults to **0** and is **1 in the New Dawn preset**;
`gNewDawn.Vfx.Strength` (0–2) scales emission, `gNewDawn.Vfx.Embers` (0–1) is ember density.
`gNewDawn.Vfx.Debug=1` logs recognised texture paths, recorded sprite positions and draw counts.

## How it works
- The interpreter classifies a loaded texture by its resource path (`Interpreter::VfxClassify`, one table). When a
  triangle samples a classified texture it is recorded in clip space (`RecordVfxTriangle`): quad centre, extents,
  depth, the game's prim/env colours, the effect's world position (from `view_pos`) and the animation frame taken from
  the texture's number (`gEffDust3Tex` → frame 2/7). The two triangles of a quad merge into one sprite.
- While the renderer can redraw them (`VfxReplacing()`: HDR world active and the program usable) the native triangles are
  skipped; otherwise the native effect draws as before. Positions, scale, colour and fade all come from the game's own
  draw, so lifetimes/counts are unchanged. No game state is read or written.
- At the scene-end marker `PostFxGL::RunVfx` (`gfx_opengl_vfx.cpp`) draws the sprites: instanced billboards, additive
  HDR for fire/glow/sparks, premultiplied alpha for dust and bomb smoke, soft-depth against the opaque depth copy. Bloom
  then picks the HDR flames up.
- **Embers** are instanced per flame, deterministic in renderer time and the flame's quantised world position; they
  rise, drift and cool; no RNG, no game state.
- Dust is tinted by the sun shadow ambient/direction colours when shadows are on, so night dust is dark.

## Classes (all CC0 art)
| class | native texture | replacement |
| --- | --- | --- |
| flame | `gEffFireTex`, `gEffEnemyDeathFlame*`, `gameplay_keep_Tex_01A8B0/01AAB0` (En_Light's small candle flame, `VFX_CANDLE`: forced alpha because that draw leaves prim alpha at 0) | animated **fire flipbook** (64-frame loop, two cross-offset layers), the game's prim/env colours, additive and partly transparent, native size and bottom anchor, plus embers. Covers every actor/effect that draws through `gEffFire1DL/2DL` (torches, chandeliers, Poes, burning enemies, burning Link) |
| glow | `gameplay_keep_Tex_08FBE0` (Lights_DrawGlow, alpha 50 only) | soft HDR halo (`circle_05`) |
| dust | `gEffDust1-8`, `gEffHitmarkDust1-8` | two lit smoke layers (`smoke_04/07`) |
| bomb | `gEffBombExplosion1-8` | HDR fireball (`fire_01/02`) cooling to a dark smoke layer (`smoke_01`) + flash. Verified with a real thrown bomb (fixture below) |
| hit | `gEffHitmarkRed/WhiteMetal1-8` | HDR spark + star (`spark_01`, `star_01`) |
| spark | `gElectricSpark1-4` | blue-white HDR spark |
| sparkle | `gameplay_keep_Tex_054AF0` (`gEffSparklesDL`) | crisp HDR star (`star_01`) with a soft halo. Recorded in game (Clock Town, 48 instances) but never visible in a frame: **not visually verified** |

Art: smoke/spark/star/halo sprites from the Kenney Particle Pack (CC0, https://kenney.nl/assets/particle-pack), unmodified, and
`fire_flipbook_8x8.png`, an authored asset (Blender Mantaflow sim rendered to a sprite sheet; scripts in
`tools/newdawn/vfx-flipbook/`). Files live in `<gNewDawn.Assets.Path>/vfx/`, default `assets/newdawn/vfx` beside the game
(no cvar needed; captures resolve it through the binary's symlinked `assets/`). Provenance: `~/dev/ghm-mods/assets/vfx/MANIFEST.md`.
OpenGameArt blocked automated downloads, so no third-party flipbook was used. Missing files disable the feature and leave the
native effects. Art is not committed (only the authoring scripts are).

## Evidence (2026-11-14, RX 7800 XT, 1920×1080, preset + full mods)
- Off identical to main: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`),
  main `a358fa2d6` build vs this branch, 2 runs each: all four PNG SHA-256 `440c1ac3…`; also identical with VFX on (no
  recorded effects in that view). 0 GL errors (`MESA_DEBUG=context`) over all 19 capture points with VFX on.
- GPU stage 15 (`gNewDawn.PostFx.Debug.Stats=1`; stage 14 is GI): **0.002–0.012 ms** with one torch + glow + embers (Clock Town, night; after the GI merge).
- Before/after: `~/dev/ghm-mods/showcase/vfx/`.

## Limits
- Texture-path classification only: flames not drawn through those textures (fire/ice/light arrows, the flat fire texture painted on the Inn firebox mesh) keep the native look. Lamps that use the glow texture with a different alpha stay native.
- Fire/ice/light arrows (`gFireArrowTex`/`MaskTex` etc., a 43-vertex model, not a quad) are **not replaced**: the model would need a whole-display-list accumulate path, and the bow fixture never nocked a magic arrow in three attempts.
- Ikana Graveyard: the first room has no fire/light actors, so no graveyard fire was found.
- Screen-aligned sprites approximate the native camera-facing quads; no lighting from the flame onto the world (the game's
  own point light is untouched).
- Sparkles/rupee glints are not covered by a captured test.

## Extras evidence (2026-11-14, `feature/vfx-extras`)
Images: `~/dev/ghm-mods/showcase/vfx/extras/`.
- **Stock Pot Inn candles**: `En_Light` with negative params draws `gameplay_keep_DL_01ACF0` (textures `01A8B0/01AAB0`); now a flipbook flame (`inn_candle_{off,on}.png`).
- **Inn fireplace**: `En_Light` (positive params) in the kitchen at (-330,-24,76) already draws through `gEffFire1DL`; the flipbook fills the firebox (`inn_fireplace_{off,on}.png`).
- **Bomb**: thrown bomb in Termina Field; HDR fireball (white-core, not clipped to a screen wash) then a dark smoke plume (`bomb_{fireball,smoke}_{off,on}.png`; on/off frames are a few ms apart).
- **Dust**: running/rolling on sand (Great Bay coast). The class was too heavy and orange on grey sand, so dust is now desaturated 45% toward luma, alpha cap 0.4, scale 0.85 (`dust_run_{off,on}.png`).
- GPU stage 15: **0.010 ms** (Inn: candles + fireplace + embers), 0.003-0.004 ms (dust run). 0 GL errors (`MESA_DEBUG=context`) over the Inn, bomb and dust runs.
- Off identity: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`, GI/Atmos wind/particles/weather off): main-base reference build x2, this branch with `Vfx.Enabled=0` x2 and with VFX on (no effects in view): all five PNGs share SHA-256 `247b7072…`.
- Private fixtures (not committed; `~/dev/ghm-mods/work/vfx2/`): `run.py` wraps `capture.py` with an actor dump (reads `gPlayState` actor lists via /proc/pid/mem), timed XTest key presses (C-up first person, C-left bomb, A/WASD run and roll) and timed memory pokes (inventory/button items); `bombs.sh`, `roll.sh`, `arrow.sh`. No game code is changed.
