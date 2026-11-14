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

- Casters are recorded in `GfxSpTri1` before the interpreter's frustum and back-face rejection, so anything the game *submits* casts. What the
  game never submits is missing. Reading `z_room.c`: `Room_DrawCullable` skips a room mesh entry only by depth of its bounding sphere
  (entirely behind the camera plane, or beyond `zFar`), not by field of view, so off-screen-to-the-side room meshes are drawn and recorded;
  meshes wholly behind the camera are not (a hill behind the camera does not shadow what is in front when the sun is behind the camera).
  Actors are skipped by their culling volume (`Actor_CullingCheck`, or `Ship_CalcShouldDrawAndUpdate` with
  `IncreaseActorDrawDistance`), so distant actors do not cast. Recording casters earlier would mean walking room mesh data ourselves
  (as the MM3D track already does with its own geometry) or changing game culling, which is out of scope; not done.
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
