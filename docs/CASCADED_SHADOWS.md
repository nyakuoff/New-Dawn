# Cascaded sun shadows

`gNewDawn.Shadows.Cascades` (1–4, default 1, **4 in the preset**) and `gNewDawn.Shadows.Distance` (1000–12000, default 7000, view-axis units).
1 is the old single map (`Shadows.Radius` around Link) and is unchanged. 2–4 split the same `MapSize` map into an atlas of 2×2 equal tiles
(4096 → four 2048 tiles), one cascade per tile, so memory is unchanged and every consumer keeps its sampler and texture unit
(no new sampler or texture array). Menu: Enhancements → New Dawn → Shadows.

## How it works

- `PostFxGL::BuildCascades` (gfx_opengl_postfx.cpp): practical split scheme (lambda 0.85, start 20) over the view frustum from the exact inverse
  camera matrix, out to `min(Distance, zFar)`. At 7000 and 4 cascades the splits are about 200 / 590 / 1750 / 7000.
  Each cascade fits a bounding sphere of its frustum slice (size does not change when the camera turns) and its centre snaps to the texel
  grid in light space, so shadow edges do not shimmer. Slices overlap by 12% so the next cascade can cross-fade over the last 12% of the nearer
  one. Tiles keep a 5-texel guard (never filtered across a tile edge); a sample that falls off a tile uses the next cascade.
- Each cascade matrix maps world straight to atlas uv. The caster pass draws all recorded triangles (and the foliage batches, so alpha-tested
  casters and sway-displaced foliage work as before) once per tile with a scissor; `GL_DEPTH_CLAMP` keeps casters in front of a tile's tight
  near plane. Depth bias and normal offset scale with the tile's texel size relative to the old map.
- `kNewDawnCsmLib` (gfx_opengl_postfx.h) is shared GLSL: `csmLit()` picks the cascade from the view-axis depth, blends and fades to lit
  beyond the last cascade. The consumer supplies `csmTap()` (its own filter). Consumers: shadow apply (12-tap rotated Poisson, same as
  before), shaft march, volumetric fog march, GI sun-visibility, ambient-particle sun lighting. The fog marches through the cascades by each
  step's own distance, so it needs no "far cascade" special case.
- The cascade code is compiled into **separate programs** (`#define CSM`); the single-map programs compile from the original text, so
  Cascades=1 is the old pipeline exactly.
- Debug: `gNewDawn.Shadows.Debug=5` tints by cascade (red, green, blue, yellow = near to far); 2 is the lit mask; 3 shows the atlas.

## Evidence (RX 7800 XT, 1920×1080, MSAA 4, full preset and mods, MESA_DEBUG=context)

- Cascades=1 equals main: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`, GI/Wind/Particles/Weather off):
  main ×2 and ours ×2 all have SHA-256 `247b7072…`. Outdoors the frozen scene is not run-to-run deterministic (actor/freeze timing), so
  the outdoor check uses the shadow-mask view (`Shadows.Debug=2`, Ikana): two main runs and one Cascades=1 run produce the same SHA `64fe5f04…`;
  other Cascades=1 runs differ on a few thousand pixels at shadow edges near Link and animated actors, as a main build of the same commit built elsewhere does (3426 px).
- No GL errors in any run. GPU, shadow stage, same scenes: Termina 0.265 → 0.298 ms, Great Bay 0.278 → 0.256, Ikana 0.360 → 0.338,
  Swamp 0.289 → 0.471, South Clock Town 0.311 → 0.322 (budget +0.6 ms).
- Images: `~/dev/ghm-mods/showcase/csm/` (`*-before/after.png`, `split-*.png` left = main, right = cascades; `crops/near-*.png` 1:1;
  `mask/`, `cascade-tint/`, `burst-*-shadow-mask.png`). Near shadows are sharper (Great Bay 1:1 crop: Link's shadow silhouette is crisper, with the arm and sword edge resolved);
  far casters appear out to 7000 (Ikana far structure, Great Bay boat and cliff shadows, Termina walls). A walk burst and a turn burst of the
  shadow mask show no seam popping or crawl (judged by eye; by construction the fit is texel-snapped).

## Limits and the culling question

- Casters are recorded in `GfxSpTri1` before the interpreter's frustum and back-face rejection, so anything the game *submits* casts.
  What the game never submits is missing. Reading `z_room.c`: `Room_DrawCullable` skips a room mesh entry only by depth of its bounding
  sphere (entirely behind the camera plane, or beyond `zFar`), not by field of view, so off-screen-to-the-side room meshes are drawn and
  recorded; entries wholly behind the camera are not. That gap is closed by **shadow coverage** below. Actors are skipped by their culling
  volume (`Actor_CullingCheck`, or `Ship_CalcShouldDrawAndUpdate` with `IncreaseActorDrawDistance`), so distant or off-screen actors still
  do not cast (see Coverage limits).
- Sphere-fit wastes some tile area on wide frustums; far-cascade texels are about one pixel at the far end of the cascade (≈8 world units at 7000).
- The shaft march (Mode 0) still uses `Shadows.Radius` for its length and density.

## Smooth sun motion (fix #82, 2026-11-14)
Shadows used to teleport: the game stores the light direction as `s8` (steps of 1/120, about 0.5 degrees), so the sun, and with it every
shadow, moved in jumps (a 0.5 degree step moves a shadow 7000 units away by about 60 units). `NewDawnSunDirection`
(NewDawnPostFx.cpp) re-evaluates the formula of `Environment_UpdateLights` (`-sin(t-12h)*120, cos*120, cos*20`) from the clock without
the truncation when time-of-day lighting is active and the result matches the stored direction within 1 step; otherwise the stored
value is used. Read-only; `gNewDawn.Shadows.SmoothSun` (default 1) switches back. Texel snapping of the cascade centres is unchanged
and does not step: the snap is relative to the continuously rotating light basis.
Verified with an accelerated clock (`run.py --clock 7 --clock-rate 0.04`, South Clock Town, shadow-mask burst): the fraction of pixels that
change between frames alternates 2.4% / 11.1% / 1.5% / 1.4% / 1.4% before and falls steadily 12.0 -> 3.7% after; a shadow edge holds for
5-8 frames and then jumps 2-3 px before, and moves 1 px at a time after (`showcase/fix-82/shadow-mask-timelapse-*.png`).
Not covered: the water glints and the grass sun still read the stored direction.

## Shadow coverage: casters the game culls (`gNewDawn.Shadows.Coverage`, roadmap item 4)
Default 0; **1 in Ultra and High**. Menu: Advanced -> Lighting & Shadows -> Shadow Coverage. Render-only.

**Problem.** `Room_DrawCullable` never draws an entry whose bounding sphere is wholly behind the camera plane or beyond `zFar`, so a hill
or building behind the camera did not shadow what is in front of it (sun behind the camera, long evening shadows), and its shadow
appeared the moment the camera turned far enough for the entry to be drawn.

**How it works.**
- `NewDawnShadows_RoomCulled` (NewDawnPostFx.cpp) runs right after `Room_Draw` (one added call in `z_room.c`, skipped for MM3D rooms). It
  repeats the game's own depth test on the same room data and camera (`viewProjectionMtxF`, `projectionMtxFDiagonal`, `lightCtx.zFar`) and
  collects the cullable entries the game just skipped. Of those it keeps the ones that can shadow anything within the shadow range:
  within `min(Shadows.Distance, zFar)` of the eye, or whose shadow ray (away from the light, up to twice that range) passes through that
  ball. The test uses the eye position only, never the view direction, so the set never changes just because the camera turns.
- Their opaque display lists go into a small list allocated from the frame's graphics pool and referenced by a new marker pair
  (`NEWDAWN_POSTFX_STAGE_SHADOW_ONLY_BEGIN/END`). The interpreter runs the list only while casters are being recorded and the post-fx
  path is active (`mShadowOnly`): `GfxSpVertex` computes just the world position, `GfxSpTri1` records the triangle with the ordinary caster
  state test and returns, nothing is drawn, and the END marker restores the RSP/RDP state, segment table and matrix counters that the list
  touched. With the effect off, on another backend, or with no shadows recorded, the list is never executed, so it cannot draw anything.
- Game state, actor culling, draw order and the display lists the game itself builds are untouched; only extra, render-side-only
  commands are appended. `gNewDawn.Shadows.CoverageDebug=1|2` logs the caster counts (2: per frame, with the camera yaw).

**Evidence** (RX 7800 XT, 1080p, Ultra + community mods, `MESA_DEBUG=context`, `~/dev/ghm-mods/showcase/shadow-coverage/coverage/`).
- Triangles added per view (yaw 0/90/180/270, 17:30): Great Bay Coast 969/574/128/553, Deku Palace 159/143/336/229, Woodfall exterior
  0/190/476/216, Mountain Village 30/51/371/319, Termina Field 70/1027/1275/1093/384/50 (six yaws), South Clock Town 0-132 (its room is
  almost not cullable). Typical frame has 11-13k casters.
- Great Bay Coast mask view at 17:30 (`offon-diff-gbm.png`): with coverage a long shadow band (yaw 0) and a pole shadow (yaw 270)
  appear that the game's culling removed. In the dusk full render (`offon-diff-gb.png`, `offon-diff-tf.png`) the same differences are
  small, because the sand and terrain are dark and ambient-lit at dusk; the effect is mainly the absence of popping, not a new look.
- Rotation burst, Great Bay Coast 17:30 (camera turned about 150 degrees in 2.2 s, per-frame caster counts, `caster-count-vs-yaw.png`):
  without coverage the caster count steps by +205, +214, +219 at 42, 65 and 77 degrees as culled entries come into the draw range; with
  coverage those steps are gone (the whole curve sits about 960 triangles higher and is flat there). Mean absolute step 22.8 -> 16.2
  triangles per frame (-29%). The mask view of the burst shows no visible shadow pop in either run over these frames
  (`rotation-burst-mask-off-top-on-bottom.png`); steps that remain (for example +404 at 57 degrees, the same with and without) are
  entries or actors the game starts drawing, not entries it culled in range.
- Off identity: frozen Stock Pot Inn, main vs this branch with `Coverage=0 PointLights=0`: SHA-256 `09f407ea2cecf510` in all four runs
  (two of each). Outdoors the frozen scene is not run-to-run deterministic (see above); with TAA off, main vs main differs on 19,847 px
  and off vs main on 331 to 18,169 px, the same noise floor. 0 GL errors in every run.
- Cost (300-frame stage averages): sun-shadow stage +0.002 ms (Great Bay Coast 0.341 -> 0.343), +0.014 ms (Termina Field 0.416 -> 0.431),
  +0.001 ms (Deku Palace); the pass itself costs 0.05-0.20 ms of render-thread CPU per frame (vertex transforms of the added entries).
  Data: `showcase/shadow-coverage/data/README.txt`.

**Coverage limits.**
- Actors are not covered. Calling a culled actor's `draw` function would run game code that may have side effects, which is out of scope;
  a render-side cache of static actor casters (keyed by display list and matrix, evicted when the actor is gone from view range) was
  designed but not built. Distant or off-screen trees, props and NPCs therefore still cast only while the game draws them. In South Clock
  Town (almost no cullable room entries) coverage adds nothing and the remaining pop is from actors.
- Only opaque casters of cullable rooms are added (no foliage/cut-out casters, no MM3D rooms, normal/image rooms are never culled).
- Entries farther than the range and not on a shadow ray into it are skipped on purpose (cost); raise `Shadows.Distance` for more.
