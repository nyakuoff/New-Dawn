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
| flame | `gEffFireTex`, `gEffEnemyDeathFlame*` | 3 flame layers (Kenney `flame_05/06`), game prim/env colours, tip sway, halo, embers |
| glow | `gameplay_keep_Tex_08FBE0` (Lights_DrawGlow, alpha 50 only) | soft HDR halo (`circle_05`) |
| dust | `gEffDust1-8`, `gEffHitmarkDust1-8` | two lit smoke layers (`smoke_04/07`) |
| bomb | `gEffBombExplosion1-8` | HDR fireball (`fire_01/02`) cooling to a dark smoke layer (`smoke_01`) + flash. **Not triggered in a capture** |
| hit | `gEffHitmarkRed/WhiteMetal1-8` | HDR spark + star (`spark_01`, `star_01`) |
| spark | `gElectricSpark1-4` | blue-white HDR spark |

Art: Kenney Particle Pack (CC0, https://kenney.nl/assets/particle-pack), unmodified copies in
`<gNewDawn.Assets.Path>/vfx/*.png`; provenance in `~/dev/ghm-mods/assets/vfx/MANIFEST.md`. Nothing is committed.
Missing files disable the feature and leave the native effects.

## Evidence (2026-11-14, RX 7800 XT, 1920×1080, preset + full mods)
- Off identical to main: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`),
  main `a358fa2d6` build vs this branch, 2 runs each: all four PNG SHA-256 `440c1ac3…`; also identical with VFX on (no
  recorded effects in that view). 0 GL errors (`MESA_DEBUG=context`) over all 19 capture points with VFX on.
- GPU stage 14 (`gNewDawn.PostFx.Debug.Stats=1`): **0.004–0.007 ms** with one torch + glow + embers (Clock Town, night).
- Before/after: `~/dev/ghm-mods/showcase/vfx/`.

## Limits
- Texture-path classification only: flames not drawn through those textures (candles/fireplace models in scene
  geometry, fire arrow, other actors) keep the native look. Lamps that use the glow texture with a different alpha stay native.
- Screen-aligned sprites approximate the native camera-facing quads; no lighting from the flame onto the world (the game's
  own point light is untouched).
- Sparkles/rupee glints and bomb explosions are not covered by a captured test.
