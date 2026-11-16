# 3D grass (render-only)

`gNewDawn.Grass.Enabled` (default 0, **1 in the New Dawn preset**, needs HDR). `Density` (0–2, default 1), `Distance`
(world units, default 1000) and `Height` (multiplier, default 1). Nothing here reads or changes game state beyond the draw
stream, the player's position and the sun; there is no collision, hitbox, RNG or animation involvement.

## How it works

1. **Recognition.** `tools/newdawn/grass_textures.py` turns the "grass" class of `material-classification.json` into
   `libultraship/src/fast/newdawn_grass_textures.inc` (205 scene-texture basenames, tiny detail tiles skipped; textures of
   interior scenes are dropped: every scene the game marks `RESTRICTIONS_INDOORS` plus a few enclosed ones, so rugs,
   upholstery and planter soil never qualify). While the
   marker-bracketed world is being drawn, the interpreter (`RecordGrassTriangle`) keeps the **world-space triangle** of
   every opaque, non-decal, non-cut-out draw whose tile-0 texture is in that list, before frustum rejection (tufts of an
   off-screen triangle can still reach into view). Triangles steeper than about 32° (`|n.y| < 0.85`) are dropped, so walls, banks and moss-covered slopes get no grass. `gNewDawn.Grass.Debug=1` logs the textures and triangle counts, `gNewDawn.Grass.Only=<substr>`
   isolates one texture.
2. **Scatter (renderer, `gfx_opengl_grass.cpp`).** At the opaque-end marker, before the depth copy, the world is cut into a
   fixed 20-unit grid. Every cell that overlaps a recorded triangle, lies within `Distance`, and is inside a conservative
   frustum test becomes a 12-float record, grouped by how many of its 32 tuft slots can be visible at that distance
   (32/16/8/4; one instanced draw per group, attribute divisor = slot count). The vertex shader derives each tuft position
   from a hash of (cell x, cell z, slot), so placement depends only on the world position: it never changes with camera
   motion or how the ground mesh is split. A tuft is kept only if its position falls inside the triangle (barycentric test
   in XZ) and its rank is below the distance density curve; the same curve makes far tufts larger so coverage holds, and
   the last 20 % of `Distance` shrinks the tufts to nothing, so nothing pops while walking.
3. **Tuft geometry and art.** Two crossed quads (24 vertices, two height segments for bending) per tuft, textured from a
   self-rendered 2×2 atlas of side-view tufts (`tools/newdawn/grass-tufts/render_tufts.py`; asset pack
   `mm/assets/newdawn/grass/`, loaded from `<gNewDawn.Assets.Path>/grass/tuft_atlas.png`; the feature stays off and
   logs a warning if the file is missing). Alpha-to-coverage with a sharpened ramp at MSAA > 1, a 0.45 cut-out otherwise.
   Sharpness (#83, "our grass looks blurry"): the atlas is now 2048² (`render_tufts.py OUT 7 1024`, any square size from 256 to
   4096 loads), sampled with 16x anisotropic filtering (the tufts are seen at grazing angles; it was 1x) and a LOD bias of
   -1.0 with alpha-to-coverage (-0.5 without; `gNewDawn.Grass.LodBias` overrides). The old 1024 atlas smeared into broad
   stippled patches. Cost: bias -1.0 0.86 ms vs 0.68 ms at -0.5 in the dense meadow (-1.5 doubles it, so it was rejected).
4. **Colour and light.** Each tuft samples the **ground colour behind its root**, copied at the ground-end marker, before any actor is drawn (a single-sample copy of the
   HDR world: 5 taps, ±5 px), multiplies it with the atlas shading (dark root, bright tip), slightly saturates it, applies
   a slow world-space dry-straw ↔ lush-green patch variation and a small per-tuft hue drift, and adds a sun-behind
   translucency term. Because the ground colour already contains per-pixel lighting, fog, time of day and local tint, the
   grass matches it at noon, dusk and in the swamp without any re-implementation. Tufts write colour and depth into the
   HDR world *before* the depth copy, so the AO, sun-shadow, local-light and GI passes treat them as ordinary geometry.
5. **Wind.** The shared `newdawn_wind.h` direction, gust and analytic travel integral drive a travelling wave, a cross
   ripple and a fast flutter. Tips move and roots stay planted (bend weight is height squared). Amplitude follows
   `gNewDawn.Atmos.WindScale`; the grass sways whether or not `gNewDawn.Atmos.Wind` is on.
6. **Player.** Tufts right at Link's feet are laid low (the tightened zone from #66, unchanged).
7. **Bend and spring-back (`gNewDawn.Grass.Bend`).** The game side hands over positions only: Link plus up to 11 grounded,
   moving NPCs/enemies (read-only snapshot, `NewDawnFillGrassPushers`). The renderer keeps a 24-sample wall-clock trail
   (a sample every 6 units of travel; a teleport leaves none) and the vertex shader leans every tuft away from the live positions
   and the trail: radius 24 for Link (trail 19), 18 for others, strongest at the feet, blades return over about 1.2 s with a
   small damped wobble (`exp(-3.2 t)`). Tips move (weight height squared), roots stay planted; nothing is written back.
8. **Flowers and clover (`gNewDawn.Grass.Flowers`, `FlowerDensity`).** A second instanced pass over the nearer cells (32- and
   16-slot bands only, one height segment) with its own hash salt; patches come from a world-space noise, a patch is mostly one
   kind, kinds from `tools/newdawn/grass-tufts/render_tufts.py OUT 7 1024 flower_atlas.png` (CC0: daisy, buttercup, violet
   wildflower, clover with white heads). They keep the indoor gate and the slope test (same recorded triangles). Fade out by
   45 % of the grass distance. Region factor in `NewDawnFillGrassFlowers`: Termina Field 1.0, Romani Ranch 1.1, Milk Road and
   cleared swamp 0.8, spring Mountain/Goron villages 0.55, Woods of Mystery 0.45 (mostly clover), Clock Town 0.45, Ikana 0.18
   (dry yellow), and **none** in winter Mountain Village, winter Goron Village and its path, Path to Snowhead, Snowhead, Path to
   Mountain Village, and the poisoned swamp. No file, no flowers (a warning is logged).
9. **Light through the blades (`gNewDawn.Foliage.Light`, `Foliage.Strength`).** The tuft translucency lobe is wider and gets a
   sun-overhead term on the tips; with the toggle off the original term is used unchanged.

## Foliage light on trees and bushes

`gNewDawn.Foliage.Light` (default 0, 1 in Ultra/High/Medium) and `Foliage.Strength` (0–3, default 1). The wind scope
(`NewDawnHDR_WindScope`) is now emitted when either the wind or this feature is on and carries two bits (sway, light), so
the cut-outs of the plant list (#83: `Actor_IsWindFoliage` actors, plus plant-classified room textures) are tagged. The
interpreter tags a draw when the cut-out render mode is set (actor foliage clears `texture_edge` when its combiner uses alpha,
so the test is `CVG_X_ALPHA`), flushes on changes and hands the renderer the actor's origin and the inverse projection. In
`default.shader.glsl` the fragment unprojects its own depth to a world position (no new vertex attribute, no new shader option),
then adds sun transmission when the sun is behind the card as seen from the camera (`pow(dot(V, L), 2)` times a facing term from the
position derivatives, tinted by the leaf's own hue, so dusk gives orange rims), and darkens the interior toward the trunk
(horizontal distance under 60 units of the actor origin, height above its base). Sun direction and colour are the grass's (the
game's dirLight1), colour scaled like the grass. Room geometry that is not plant-classified, and the foreground pine
trees of Termina Field, take no part (see Limits).

## Measurements (RX 7800 XT, 1080p, preset, MSAA 4, TAA; mean of 300 frames, same binary, features on vs off)

| Scene | grass stage off → on | whole frame off → on |
|---|---|---|
| Termina Field meadow (2041,40,610) | 1.26 → 1.73 ms | 6.39 → 7.03 ms |
| Cleared swamp | 1.03 → 1.31 ms | 5.99 → 6.37 ms |
| North Clock Town | 0.125 → 0.145 ms | 4.60 → 4.62 ms |

Roughly 0.3–0.5 ms in a dense meadow (flowers and bend), nothing measurable where there is little grass. 0 GL errors
(`MESA_DEBUG=context`) in Termina Field and North Clock Town walking at dusk.

## Measurements (RX 7800 XT, 1080p, preset, MSAA 4)

Dense Termina Field meadow (stand at 2041,40,610): grass stage **0.81 ms** with three-quad tufts it was 1.2 ms (overdraw of
alpha-tested cards dominates). Termina Field default point 0.22 ms, cleared swamp / North Clock Town 0.4 ms, MSAA 1
0.37 ms. The AO stage rises by about 0.2 ms in dense meadows because it now sees tuft depth. Fixed cost with no tufts
visible: 0.085 ms (the ground copy).

## Gating and identity

Grass is only enabled in time-of-day (`LIGHT_MODE_TIME`) scenes that are not indoors. Time-of-day light mode alone is not
enough: the Mayor's Residence (#83) uses it, and its planter and couch got tufts. `NewDawnSceneIsIndoors` therefore also
treats a scene as enclosed when the game's own per-scene restrictions disable both the Song of Storms and Double Time (the
`RESTRICTIONS_INDOORS` set), and the texture list above drops interior-scene textures as a second layer. In the frozen Stock Pot Inn fixture (noon, frozen at frame 30, `final_fixture.py`
recipe plus `LocalFlicker=0`, `Vfx.Embers=0`, dither off) grass on vs off differs by at most 4 levels (no tufts), the same
order as two runs of an unmodified `origin/main` build (`5b986cedd`, built in a separate worktree). **Byte identity was not
established**: on the day of this check the fixture itself was not deterministic (main vs main differed in 360k pixels,
mostly Link and lit surfaces, while other agents loaded the machine). With the cvar off the only code run per frame is a
no-op marker flag check; the interpreter records nothing and the renderer does not run the pass. Images:
`~/dev/ghm-mods/showcase/grass/identity-main-vs-main-noise.png`.

## Limits

- #75 (2026-11-16): the hue classifier left Romani Ranch's yellow-green lawn tile (`Z2_F01Tex_00ED40`, `01BA00`, and the same tile in Ranch Milk Road/Ikana Valley entrance/Gorman Track/Laundry Pool/Moon) as "stone", so the ranch had **no tufts at all** (`Grass.Debug=1`: 0 triangles). `grass_textures.py` now also takes non-grass-class outdoor tiles with hue 60-70, saturation >= 0.55, luminance >= 0.5 and size >= 512 (checked by eye; the `20SICHITAI2` gradient strips match the numbers and are excluded). `gNewDawn.Grass.Debug=2` additionally logs, per texture, the level opaque ground triangles that were NOT recorded, to find the next missing lawn. Images: `showcase/bugsweep/75-*`. Still unchecked: East/South Clock Town have no classified lawn in the visited spots (cobbles), Great Bay's beach has tufts only on the 5 grass textures present. The "colour behind props" report was not reproduced: tufts take the ground-copy colour at their root (the ground copy has no actors), so they miss only the prop's own shadow/AO, which the later passes apply to tufts as geometry.
- Placement follows classified textures only. The classification is a hypothesis (hue/saturation based), so a texture
  that is mossy stone or dirt but classified grass would get tufts; check `gNewDawn.Grass.Debug`. Ground whose real
  texture is classified `other`/`dirt` (parts of the ranch, Clock Town lawns) gets none.
- Ground colour is copied at a new `GROUND_END` marker (after the room, before `Actor_DrawAll`), so tufts never take Link's or any actor's colours; it also lacks anything actor-drawn (trees, props), which simply show their own colour. Without the marker the copy falls back to the finished scene.
- No grass on 4K tiles whose draws are cut-out or translucent.
- Foliage light only reaches draws in the wind scope with the cut-out render mode: the Termina Field foreground pines and the Romani
  Ranch pine were not affected, the distant forest backdrop and plant-classified room cut-outs were. Widening it means
  classifying more room textures (`grass_textures.py`). The effect is gentle by design at the default strength; the sun must be
  behind the leaves.
- Flowers use the same ground triangles as the tufts, so ground whose texture is not classified grass (much of Romani Ranch's
  dirt-ish meadow) gets none.
- The bend trail is wall-clock based: while the game is paused or frame-stepped the blades still spring back.
- Off identity: with Foliage.Light, Grass.Bend and Grass.Flowers at 0 the new binary differs from a baseline build in the frozen
  Stock Pot Inn fixture by at most 4 levels, inside the main-vs-main noise of the same session (max 8). Not byte-identical, the
  fixture itself is not deterministic here.
- Romani Ranch could only be captured from spawn 4 (the other spawns play the arrival cutscene on a fresh save).
