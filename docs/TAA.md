# Temporal anti-aliasing (TAA)

Roadmap item 1. Removes shimmer and crawl on fine detail (3D grass tufts, POM edges, foliage cut-outs, fences, thin
geometry at a distance, specular glints) by jittering the 3D world's projection by a sub-pixel amount every frame and
blending the frames in scene-referred light. Render-only: no game state, geometry, collision, animation or timing is
read or changed. OpenGL only, needs the Modern Lighting Pipeline (`gNewDawn.HDR.Enabled`).

| Setting | Default | |
| --- | --- | --- |
| `gNewDawn.TAA.Enabled` | 0 (**1 in Ultra and High**) | Graphics > Advanced > Image & Colour |
| `gNewDawn.TAA.Sharpen` | 0.7 | 0..1, "TAA Sharpening" slider. 0 = off |
| `gNewDawn.TAA.Feedback` | 0.9 | 0..0.98, history weight on a still pixel. Higher = steadier, softer, more ghosting |
| `gNewDawn.TAA.Debug` | 0 | 1 = history weight, 2 = (clip amount, actor mask, weight) as RGB |
| `gNewDawn.TAA.DebugPan`, `.DumpFrames`, `.DumpDir` | 0 | measurement fixture, see Verification. Do not leave on |

With TAA off nothing is jittered and no pass runs: the frozen Stock Pot Inn is byte-identical to main (see below).

## Design

**Jitter (interpreter).** `Interpreter::BeginTaa` runs right after the renderer opens the HDR world (`WORLD_BEGIN`). It
picks a 16-sample Halton(2,3) offset (within ±0.5 px) and, from then on until `SCENE_END`, shifts every **perspective**
projection the game loads (`GfxSpMatrix`, `G_MTX_LOAD | G_MTX_PROJECTION`; the game then multiplies the view in, so the
shift ends up as the rightmost factor of the final view-projection). The matrix that was loaded *before* the marker is
shifted in place too. Orthographic loads (screen fills, HUD-style 3D) are skipped, `mTaaJitter` is cleared at
`SCENE_END`, so the HUD, 2D sprites, text, the pause menu and everything drawn after the world are never jittered.
Because the shift lives in `P_matrix`, every New Dawn pass that rebuilds positions from it (grass, VFX sprites, AO, GI,
fog, water...) is jittered consistently with the depth buffer, and the 2S2H frame interpolation needs nothing special:
the matrices are read per rendered frame, after interpolation.

**Reprojection.** No per-object motion vectors exist, so only camera motion is reprojected: this frame's un-jittered
view-projection (captured before the shift) and the previous rendered frame's, both handed to `PostFxGL::SetTaaCamera`.
The depth buffer of the finished world (single-sample copy) gives the world position of each pixel.

**Resolve (`gfx_opengl_postfx.cpp`, `kTaaShader`).** Runs in `RunSceneEnd` on the final snapshot only (`EndHdr`; the
mid-frame snapshots for copies/readbacks never touch the history), on the resolved RGBA16F world, **before bloom and the
display transform**, so bloom is temporally stable too. Colour is tone-compressed (`c / (1 + luma)`) and held in YCoCg so
bright glints cannot dominate. Per pixel:
1. The 3×3 current samples are weighted by their distance to the pixel centre *after* undoing the jitter (a Gaussian close
   to Blackman-Harris). A single jittered point sample would make a converged still image pulse once per jitter cycle.
2. History is fetched with a 5-tap Catmull-Rom filter, clipped to the neighbourhood's mean ± 1.1σ (and min/max) in YCoCg.
3. History is rejected when the reprojected point is outside the screen, or its expected view depth is outside the range
   stored around the history texel (plus a slope allowance). The range, not one texel, because at a silhouette the depth
   under a pixel flips between the near and far surface with the jitter phase.
4. Weight: 0.9, minus up to 0.12 with speed (resampling softens, so trust it less in motion), times `1 - 0.6 × clip amount`.
5. **Actors:** the stencil mask the local-lights pass already writes for character draws (#56/#82) is copied out
   (`mTaaMask`); actor pixels (dilated by the 3×3) get a 0.6σ clip and a weight of at most 0.5. Without
   `gNewDawn.Lighting.Local` there is no mask and the clip alone protects them.

History stores the unsharpened image (colour + view depth in alpha); a light contrast-adaptive sharpen (after FidelityFX CAS,
`kTaaSharpenShader`) is applied only to what goes on to bloom. It works in a square-root space on compressed colour, backs
off where local contrast is high and **fades out below perceptual luma 0.12–0.35**: nothing in the darks.

**History reset** (`PostFxGL::RunTaa`, logged with `gNewDawn.PostFx.Debug.Stats=1`): first frame, any gap in rendered
frame numbers (pause menu, the Deku-bubble LDR fallback, any frame the HDR world did not run), framebuffer resize, and
camera cuts (the eye moves more than 400 units, or the view axis turns more than about 25°, in one rendered frame:
scene/room entrances and cutscene cuts). Verified from the log: the pause menu resets on resume; a scene warp resets at the cut (and again at the arrival cutscene's own camera jumps, 570-2040 units).

**MSAA.** Independent. TAA works with the 4× MSAA resolve as input (Ultra/High keep it: smoother cut-out edges through
alpha-to-coverage) or with MSAA off (see numbers: about the cost of MSAA 4 alone, same stability, slightly stair-stepped
cut-out edges because alpha-to-coverage needs MSAA). Medium/Low are unchanged (MSAA 2/1, TAA off); TAA + MSAA 1 is the
obvious cheaper tier if one is wanted.

## Verification

RX 7800 XT, 1920×1080, gamescope headless, Ultra preset + the full local mod set, `MESA_DEBUG=context`, binary built from this
branch against the Play build of main `fc8459f1a` as reference. Fixtures are local (`~/dev/ghm-mods/work/taa/`: `run.py`
freezes game logic / drives keys, `measure.py`, `ghost.py`, `metrics.py`, `tvar.py`); images in `~/dev/ghm-mods/showcase/taa/`.

**0 GL errors** (`MESA_DEBUG=context`, stats on) in every burst, timing, reset and full-frame run. **Off identity:** Stock Pot Inn, 12:00, logic frozen at frame 30,
`gMatchRefreshRate=0`, `gInterpolationFPS=20`, the full deterministic-off cvar list of the agent rules, `TAA.Enabled=0`:
two runs of main and two of this branch have the **same SHA-256** (`705ef49b…`).

**Shimmer metric A: slow camera pan.** `gNewDawn.TAA.DebugPan=0.3` shifts the clip-space camera 0.3 px per rendered frame
(a slow pan, ≈ 18 px/s at 60 fps) while game logic is frozen, so the only change between frames is sub-pixel motion. 24
consecutive frames are dumped (`DumpFrames`). The global shift between each pair is registered (Lucas-Kanade, ±0.08 px
search), the earlier frame resampled by it (Catmull-Rom), and `resid` is the mean absolute difference to the later frame:
a stable image only translates, an aliased one flickers. `detail` is the mean luma gradient, a guard against "stable
because blurred". Luma 0..1, ×10⁻³; "TAA" is TAA + MSAA 4, "off" is MSAA 4 alone.

| Region | resid off | resid TAA | change | detail off → TAA |
| --- | ---: | ---: | ---: | ---: |
| Termina grass | 15.37 | 4.99 | **−68 %** | 22.5 → 16.3 |
| Termina pillar (cut-out edges) | 1.79 | 1.11 | −38 % | 8.9 → 9.3 |
| Termina distant fence | 1.37 | 0.97 | −30 % | 6.1 → 7.0 |
| Swamp grass, left | 23.02 | 4.81 | **−79 %** | 27.6 → 12.3 |
| Swamp grass, right | 41.85 | 8.73 | **−79 %** | 47.8 → 24.8 |
| Swamp banana foliage | 3.10 | 1.44 | −53 % | 11.8 → 12.2 |
| Swamp dock and poles | 3.64 | 2.20 | −40 % | 11.9 → 11.2 |
| South Clock Town POM plank floor | 1.93 | 1.34 | −30 % | 13.7 → 13.8 |
| South Clock Town brick wall | 6.80 | 2.28 | −66 % | 28.3 → 25.4 |
| South Clock Town bunting strings | 5.99 | 2.23 | −63 % | 31.2 → 29.7 |

In the grass the lost "detail" is mostly the aliasing it removed (the off crops are full of dithered specks, see
`crop2x_swamp_grass_off_vs_taa.png`); the blades themselves come out a little softer. Per unit of detail, instability halves
or better everywhere (grass 0.68 → 0.31, brick 0.24 → 0.09). TAA with MSAA off is within a few percent of TAA with MSAA 4.
(North Clock Town was not usable as a capture point: the private entrance id loads an empty void; South Clock Town
supplies the brick and POM planks.)

**Metric B: static camera, game running (wind on).** Mean per-pixel temporal standard deviation of luma, 24 frames:

| Region | off | TAA | |
| --- | ---: | ---: | --- |
| Termina grass | 18.7 | 11.9 | −36 % (mean frame-to-frame change −63 %) |
| Swamp grass | 38.4 | 18.7 | −51 % (frame-to-frame −75 %) |
| Termina pillar and fence | 0.96 | 0.85 | −11 % |
| Swamp banana foliage | 0.93 | 1.41 | **+52 %** |

The foliage row is a regression: on static high-contrast cut-out edges TAA adds a faint residual twinkle (fraction of pixels
with temporal std above 2/255: 0.19 % off, 0.83 % TAA; p99.9 3.5 vs 5.2 levels). Without the silhouette-tolerant depth test
and the jitter-aware filter it was 3.6 % and 16 levels. It is below the visibility of a single level on average; it is what
is left when swaying edges cannot be reprojected.

**Ghosting.** Link running (held stick) in the swamp meadow and across South Clock Town beside an NPC, game logic running,
40 consecutive frames each: `ghost_link_swamp.png`, `ghost_link_south_clock_town.png` (top row off, bottom row TAA, 1:1).
No trailing edges or colour smears behind Link, the guard or Tatl's glow. The debug view 2 (`ghost_actor_mask_debug.png`)
shows the actor mask covering Link, the guard and a distant NPC. Not exercised: arrows, rain/snow streaks, enemies, the
camera cutting while actors move.

**Sharpness.** `crop2x_*_off_vs_taa.png` (nearest-neighbour 2× of 1:1 crops, off | TAA + MSAA 4 | TAA, MSAA off). Cut-outs,
fences, POM planks and brick are within about ±10 % of off in gradient detail; grass is smoother and clearly softer. HUD checked 1:1 in full-frame captures
(`full_*`): not jittered, not filtered. Raise `TAA.Sharpen` for crisper grass; 1.0 clearly over-sharpens.

**GPU cost** (RX 7800 XT, 1080p, stage timers averaging 300 frames, three windows each; "world" = HDR world open to the
presentation copy, the whole 3D world including every New Dawn pass, HUD and swap excluded):

| Scene | MSAA 4 only | TAA + MSAA 4 | TAA, MSAA off | MSAA off only | TAA stage (resolve + sharpen) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Termina Field | 3.32 | 3.84 | 3.28 | 3.01 | 0.38 |
| South Clock Town | 3.70 | 4.04 | 3.70 | 3.36 | 0.45 |
| Southern Swamp | 4.40 | 4.34 | 4.22 | 3.89 | 0.37 |

TAA itself is 0.32–0.45 ms. With MSAA 4 kept it adds 0.0–0.5 ms to the world; TAA with MSAA off costs the same as MSAA 4
alone (−0.2 to 0.0 ms) and keeps the stability. Window noise is about ±0.15 ms.

## Known artefacts and limits

- Without motion vectors, anything that moves by itself (wind sway, water, particles, fast effects, actors) falls back to
  the neighbourhood clip: it is never wrongly smeared, but it also is not anti-aliased across frames. Sway edges twinkle
  slightly more than unjittered (Metric B, foliage).
- Grass is a little softer than off at the default sharpen; fine, bright, one-pixel features (a thin specular line) can
  shimmer less but also fade a touch.
- TAA does not run on frames where HDR falls back to LDR (Deku-bubble frames), on the pause background, or while a secondary
  framebuffer is active; the next frame restarts from scratch.
- A hard camera cut is detected by distance and view turn; a very fast legitimate pan (> 25° in one rendered frame) would
  reset history for a frame.
- Not verified: window resize at runtime, pictograph, underwater, cutscenes with many cuts, the new menu entries on screen
  (they are two widgets in the existing registry), other backends (Direct3D/Metal have no post-processing at all).

## Upscaling (out of scope here)

The pieces an upscaler needs are in place: per-frame sub-pixel jitter in the projection, un-jittered current and previous
view-projections, a depth copy, an actor mask and a reprojecting history. Rendering the 3D world below native resolution
would need the game framebuffer sized separately from the output, jitter scaled to the render resolution, the resolve
writing at output size (the same shader with an output-sized history and the Gaussian weights evaluated at the render
grid), and sharpening and bloom running after it. The HUD would then be drawn at output resolution, as it is today.
