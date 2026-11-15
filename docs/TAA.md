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
| `gNewDawn.TAA.Debug` | 0 | 1 = history weight, 2 = (clip amount, actor mask, grass mask) as RGB |
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

6. **Grass:** the grass pass writes stencil 2 for its tuft fragments (characters write 1; the stencil is cleared at the start
   of the world whenever TAA or local lights are on), and the mask target is RG8 (red characters, green tufts). Tufts are
   thin blades that sway, so no reprojection can follow them and the soft Gaussian that suits surfaces blurs them: on tuft
   pixels the history weight is capped at 0.85, the current-frame filter is narrower (Gaussian exponent 5 instead of 2.29,
   σ ≈ 0.32 px) and the sharpen runs at full strength. Constants in `RunTaa`, tuned against the grass crops below.

History stores the unsharpened image (colour + view depth in alpha); a light contrast-adaptive sharpen (after FidelityFX CAS,
`kTaaSharpenShader`) is applied only to what goes on to bloom. It works in a square-root space on compressed colour, backs
off where local contrast is high and **fades out below perceptual luma 0.04–0.14** (display values below about 0.05 are left
alone, full strength from about 0.15): true darks get nothing, mid-dark grass and shadowed brick are sharpened. (The first
version faded over 0.12–0.35 and so also softened mid-dark grass.)

**History reset** (`PostFxGL::RunTaa`, logged with `gNewDawn.PostFx.Debug.Stats=1`): first frame, any gap in rendered
frame numbers (pause menu, the Deku-bubble LDR fallback, any frame the HDR world did not run), framebuffer resize, and
camera cuts (the eye moves more than 400 units, or the view axis turns more than about 25°, in one rendered frame:
scene/room entrances and cutscene cuts). Verified from the log: the pause menu resets on resume; a scene warp resets at the cut (and again at the arrival cutscene's own camera jumps, 570-2040 units).

**Texture units and timers.** TAA samples only the dedicated postfx lanes 8–11 (`uScene`/`uDepth`/`uTaaHistory`/`uTaaMask`,
every lane holds a complete dummy until a pass binds real data), so it cannot collide with the Milky Way (21), caustics (28) or
the water-sim units 31–33. It is timer stage 21, "TAA", in the perf overlay (`kQueryStages == NEWDAWN_PERF_STAGES` holds).

**MSAA.** Independent. TAA works with the 4× MSAA resolve as input (Ultra/High keep it: smoother cut-out edges through
alpha-to-coverage) or with MSAA off (see numbers: about the cost of MSAA 4 alone, same stability, slightly stair-stepped
cut-out edges because alpha-to-coverage needs MSAA). Medium/Low are unchanged (MSAA 2/1, TAA off); TAA + MSAA 1 is the
obvious cheaper tier if one is wanted.

## Verification

RX 7800 XT, 1920×1080, gamescope headless, Ultra preset + the full local mod set, `MESA_DEBUG=context`, binary built from this
branch (merged with main `f30d0fda8` and libultraship new-dawn `570f4d9c`); the Play build of main at that commit is the
off-identity reference. Measurement runs used the full local mod set (`sets/full`) for every variant. Fixtures are local (`~/dev/ghm-mods/work/taa/`: `run.py`
freezes game logic / drives keys, `measure.py`, `ghost.py`, `metrics.py`, `tvar.py`); images in `~/dev/ghm-mods/showcase/taa/`.

**0 GL errors** (`MESA_DEBUG=context`, stats on) in every burst, timing, reset and full-frame run. **Off identity:** Stock Pot Inn, 12:00, logic frozen at frame 30,
`gMatchRefreshRate=0`, `gInterpolationFPS=20`, the full deterministic-off cvar list of the agent rules, `TAA.Enabled=0`:
two runs of main (`f30d0fda8`, the Play build) and two of this branch, community mod set, have the **same SHA-256**
(`705ef49b…`); the same hash came out against the earlier main and the full mod set.

**Shimmer metric A: slow camera pan.** `gNewDawn.TAA.DebugPan=0.3` shifts the clip-space camera 0.3 px per rendered frame
(a slow pan, ≈ 18 px/s at 60 fps) while game logic is frozen, so the only change between frames is sub-pixel motion. 24
consecutive frames are dumped (`DumpFrames`). The global shift between each pair is registered (Lucas-Kanade, ±0.08 px
search), the earlier frame resampled by it (Catmull-Rom), and `resid` is the mean absolute difference to the later frame:
a stable image only translates, an aliased one flickers. `blade-scale detail` is the mean gradient of the 2×2-averaged luma
(blade and edge sharpness without counting 1-pixel dither speckle); the plain 1-pixel gradient is in the raw files. Luma
0..1, ×10⁻³; "TAA" is TAA + MSAA 4, "off" is MSAA 4 alone.

| Region | resid off | resid TAA | change | blade-scale detail off → TAA |
| --- | ---: | ---: | ---: | ---: |
| Termina grass | 15.4 | 9.5 | −38 % | 22.1 → 24.1 (+9 %) |
| Termina pillar (cut-out edges) | 1.79 | 1.16 | −35 % | 11.8 → 13.6 |
| Termina distant fence | 1.37 | 0.98 | −28 % | 6.9 → 8.3 |
| Swamp grass, left | 23.0 | 12.9 | −44 % | 21.5 → 20.9 (−3 %) |
| Swamp grass, right | 42.1 | 20.9 | −50 % | 37.9 → 38.8 (+2 %) |
| Swamp banana foliage | 3.10 | 1.46 | −53 % | 14.5 → 16.0 |
| Swamp dock and poles | 3.48 | 2.46 | −29 % | 13.8 → 15.7 |
| South Clock Town POM plank floor | 1.93 | 1.48 | −23 % | 17.7 → 21.0 |
| South Clock Town brick wall | 6.82 | 2.49 | −63 % | 34.3 → 37.4 |
| South Clock Town bunting strings | 5.99 | 2.52 | −58 % | 45.5 → 48.6 |

Grass is the trade-off: the first version (no tuft class) removed 68–79 % of the flicker but left grass 10–50 % softer
than off, so the grass class above gives some of the flicker reduction back for crispness (flicker per unit of detail is
still about half: grass 0.69 → 0.36, brick 0.24 → 0.09). Grass blades are now as sharp as TAA off at blade scale and the
off image's dither speckle is gone (`crop2x_termina_grass_off_vs_taa.png`, `crop2x_swamp_grass_off_vs_taa.png`). TAA with MSAA
off is within a few percent of TAA with MSAA 4. (North Clock Town was not usable as a capture point: the private entrance id
loads an empty void; South Clock Town supplies the brick and POM planks.)

**Metric B: static camera, game running (wind on).** Mean per-pixel temporal standard deviation of luma, 24 frames, and the mean
absolute frame-to-frame change (the wind sway itself is real motion that TAA cannot remove, so the second number is the
better flicker proxy):

| Region | std off → TAA | frame-to-frame change off → TAA |
| --- | --- | --- |
| Termina grass | 20.5 → 18.8 (−8 %) | 14.1 → 8.4 (**−41 %**) |
| Swamp grass | 38.7 → 35.6 (−8 %) | 24.5 → 15.9 (**−35 %**) |
| Termina pillar and fence | 1.12 → 1.02 (−9 %) | 0.56 → 0.52 (−7 %) |
| Swamp banana foliage | 0.86 → 1.43 (**+66 %**) | 0.38 → 0.76 |

The foliage row is a regression: on static high-contrast cut-out edges TAA adds a faint residual twinkle (fraction of pixels
with temporal std above 2/255: 0.18 % off, 0.87 % TAA; p99.9 3.6 vs 5.2 levels). Without the silhouette-tolerant depth test
and the jitter-aware filter it was 3.6 % and 16 levels. It is below the visibility of a single level on average; it is what
is left when swaying edges cannot be reprojected.

**Ghosting.** Link running (held stick) in the swamp meadow and across South Clock Town beside an NPC, game logic running,
40 consecutive frames each: `ghost_link_swamp.png`, `ghost_link_south_clock_town.png` (top row off, bottom row TAA, 1:1).
No trailing edges or colour smears behind Link (also with the grass class on, through the tufts around him), the guard or Tatl's glow. The debug view 2 (`ghost_actor_mask_debug.png`)
shows the actor mask covering Link, the guard and a distant NPC. Not exercised: arrows, rain/snow streaks, enemies, the
camera cutting while actors move.

**Sharpness.** `crop2x_*_off_vs_taa.png` (nearest-neighbour 2× of 1:1 crops, off | TAA + MSAA 4 | TAA, MSAA off). Cut-outs,
fences, POM planks, brick and grass are at or above off in blade-scale detail (grass −3 % to +9 %), without off's dither speckle. HUD checked 1:1 in full-frame captures
(`full_*`): not jittered, not filtered. `TAA.Sharpen` (0.7) applies to everything except tufts, which always get the full sharpen; 1.0 everywhere over-sharpens.

**GPU cost** (RX 7800 XT, 1080p, stage timers averaging 300 frames, three windows each, Ultra preset on the final merged build;
"world" = HDR world open to the presentation copy, the whole 3D world including every New Dawn pass, HUD and swap excluded):

| Scene | MSAA 4 only | TAA + MSAA 4 | TAA, MSAA off | MSAA off only | TAA stage (resolve + sharpen) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Termina Field | 4.64 | 4.98 | 4.61 | 4.00 | 0.48 |
| South Clock Town | 4.27 | 4.79 | 4.41 | 4.04 | 0.49 |
| Southern Swamp | 5.43 | 5.91 | 5.24 | 4.81 | 0.48 |

TAA itself is 0.43–0.49 ms (0.43 with MSAA off, 0.48 with MSAA 4: the resolve is a few extra mask draws plus the sharpen).
With MSAA 4 kept it adds 0.3–0.5 ms to the world; TAA with MSAA off costs the same as MSAA 4 alone (−0.2 to +0.1 ms) and
keeps the stability. The base world is more expensive than in the first measurement (3.3–4.4 ms) because the night sky and the
water simulation merged in between. A second run of the same matrix agrees within ±0.2 ms except one outlier (South Clock
Town, MSAA off only, 5.1 instead of 4.0) from other processes on the shared GPU.

## Known artefacts and limits

- Without motion vectors, anything that moves by itself (wind sway, water, particles, fast effects, actors) falls back to
  the neighbourhood clip: it is never wrongly smeared, but it also is not anti-aliased across frames. Sway edges twinkle
  slightly more than unjittered (Metric B, foliage).
- Grass flicker is reduced by 35-50 % rather than the 70-80 % an unconstrained resolve reaches: the tuft class trades some of
  it for crispness. Fine, bright, one-pixel features (a thin specular line) can shimmer less but also fade a touch.
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
