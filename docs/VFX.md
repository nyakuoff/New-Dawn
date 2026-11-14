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
| sparkle | `gameplay_keep_Tex_054AF0` (`gEffSparklesDL`, `EffectSsKirakira`: fairy sparkles, Tatl/Navi trail, rupee/item glitter, warp sparkles) | crisp HDR four-point star (`star_01`, 2.9x the native quad) plus a small halo, twinkling with the game's env alpha. Verified with a spawned fairy (`En_Elf`) |
| magic arrows | `gFireArrow*`, `gIceArrow*`, `gLightArrow*` (Tex and MaskTex of the 43-vertex glow model) | the whole model is accumulated into **one instance** (`RecordVfxArrow`) holding its screen-space axis; the renderer draws 24 HDR sprites along it (`fire_01/02` fire plume, `smoke_01`/`spark_01`/`star_01` ice, `light_01`/`star_01` light) plus a tip star and halo. The native `EffectBlure` ribbon still draws the streak. When the game scales the model up on impact (axis shorter than its width) the sprites fill a disc instead: fire ball, frost burst, light flash |

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
- Magic arrows are replaced by a sprite chain along the model's principal screen axis, not by a re-skin of the 3D mesh: the sprites are screen-aligned, so the arrow's own depth profile is approximated; the Z-test uses the model's centre.
- Ikana Graveyard: the first room has no fire/light actors, so no graveyard fire was found.
- Screen-aligned sprites approximate the native camera-facing quads; no lighting from the flame onto the world (the game's
  own point light is untouched).
- Rupee glints use the same `gEffSparklesDL`; a spawned rupee was captured (the draw is recorded), but the glints are small and the evidence images are fairy sparkles.

## Extras evidence (2026-11-14, `feature/vfx-extras`)
Images: `~/dev/ghm-mods/showcase/vfx/extras/`.
- **Stock Pot Inn candles**: `En_Light` with negative params draws `gameplay_keep_DL_01ACF0` (textures `01A8B0/01AAB0`); now a flipbook flame (`inn_candle_{off,on}.png`).
- **Inn fireplace**: `En_Light` (positive params) in the kitchen at (-330,-24,76) already draws through `gEffFire1DL`; the flipbook fills the firebox (`inn_fireplace_{off,on}.png`).
- **Bomb**: thrown bomb in Termina Field; HDR fireball (white-core, not clipped to a screen wash) then a dark smoke plume (`bomb_{fireball,smoke}_{off,on}.png`; on/off frames are a few ms apart).
- **Dust**: running/rolling on sand (Great Bay coast). The class was too heavy and orange on grey sand, so dust is now desaturated 45% toward luma, alpha cap 0.4, scale 0.85 (`dust_run_{off,on}.png`).
- GPU stage 15: **0.010 ms** (Inn: candles + fireplace + embers), 0.003-0.004 ms (dust run). 0 GL errors (`MESA_DEBUG=context`) over the Inn, bomb and dust runs.
- Off identity: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`, GI/Atmos wind/particles/weather off): main-base reference build x2, this branch with `Vfx.Enabled=0` x2 and with VFX on (no effects in view): all five PNGs share SHA-256 `247b7072…`.
- Private fixtures (not committed; `~/dev/ghm-mods/work/vfx2/`): `run.py` wraps `capture.py` with an actor dump (reads `gPlayState` actor lists via /proc/pid/mem), timed XTest key presses (C-up first person, C-left bomb, A/WASD run and roll) and timed memory pokes (inventory/button items); `bombs.sh`, `roll.sh`, `arrow.sh`. No game code is changed.

## Magic arrows and sparkles evidence (2026-11-14, `feature/vfx-magic`)
Images: `~/dev/ghm-mods/showcase/vfx-magic/` (1:1 crops, left = `Vfx.Enabled=0`, right = on; same scene, frame timing differs by a few ms).
- **Fixture (private, not committed)**: `En_Arrow` (actor 0x0F) spawned with params 3/4/5 (fire/ice/light) spawns its own `Arrow_Fire/Ice/Light` child and flies free with no parent, so no nocking, magic or input is needed. A test-only `OnActorUpdate` hook (`mm/2s2h/DeveloperTools/NdTestSpawn.cpp`, env `ND_ARROW` / `ND_ACTOR`, a no-op when unset) spawns them; `~/dev/ghm-mods/work/vfx3/arrow.sh`. Earlier agents failed to nock an arrow with timed key presses; spawning removes the dependency on game input.
- Fire, ice and light arrows each captured in flight and on impact (`{fire,ice,light}_arrow_{2,7}_off_on.png`). Fairy sparkles: `sparkles_off_on.png` (`En_Fairy` type 1).
- GPU stage 15 (`gNewDawn.PostFx.Debug.Stats=1`): fire 0.022 ms, ice 0.027 ms, light 0.057 ms, sparkles 0.014 ms (Termina Field). 0 GL errors (`MESA_DEBUG=context`).
- Off identity: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`) with the AGENT-RULES recipe plus `Fog.Volumetric=0 SunShafts.Enabled=0 Sky.Physical=0 Sky.Flare=0 Lighting.Local=0 Water.Enabled=0` (without these the Inn's window region still differs run to run, so the recipe is not enough there). A build of the base commit `5b986cedd` x2, this branch with `Vfx.Enabled=0` x3 and with VFX on (no effects in view): all six PNGs share SHA-256 `e458ebe3…`. Run before the final sparkle tuning and the merge onto main `7cd5eb65c`; renderer code runs only when VFX is recording.
- Not done (item 4): hookshot, magic-meter, Deku-bubble and mask-transformation effects; `gEffLightning*`, `gEffIce*`, `gEffPoppedDekuBubble*` are candidate textures for the same classifier but were not captured.
- Not found: Tatl's trail in a frame (the intro cutscene is skipped in the capture saves); Tatl/Navi use the same `EffectSsKirakira` sparkle that the spawned fairy shows.
