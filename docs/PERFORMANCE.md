# Performance and quality tiers

Roadmap item 8. Branch `feature/perf-tiers` (libultraship `feature/perf-tiers`), measured 2026-11-15/16.

## How it was measured
- **Hardware:** AMD Radeon RX 7800 XT (radeonsi, Mesa, OpenGL 4.6), 1920x1080 via headless gamescope, community texture mods set, real game logic (nothing frozen), Link standing still at the capture point.
- **GPU time:** the whole-frame timestamp pair of the performance overlay (`gNewDawn.Perf.Overlay`) and the per-stage timers, logged as 300-frame means with `gNewDawn.PostFx.Debug.Stats`. Each table cell is the mean of the last three 5 s windows of a ~40 s run after warm-up.
- **CPU time:** new log line `NewDawnPerf: frame ... ms wall, ... ms render-thread CPU` (same switch). It is the CPU time of the thread that walks the display lists and issues GL (it includes the interpolation and the effect recorders); the game-logic thread is not included.
- **FPS:** `gMatchRefreshRate=0`, `gInterpolationFPS=60` for the main tables (the interpolation limiter holds exactly 60 fps, so GPU ms is what shows the headroom), and 144 / 240 for the high-refresh table. VSync is not the limiter in these runs.
- **Scenes** (the capture points of `tools/newdawn/capture.py`, clock set by the fixture): Termina Field 12:00 and 17:30 (grass, flowers, clouds), South Clock Town and North Clock Town day 1 at 10:00 with the crowds, Great Bay Coast 12:00, Stock Pot Inn 23:00, Woodfall Temple, Ikana Canyon 12:00 (river). Scripts and raw logs: `~/dev/ghm-mods/work/perf-tiers/` (`measure.py`, `matrix.sh`, `runs/`); images: `~/dev/ghm-mods/showcase/perf-tiers/`.
- **Noise:** another agent's game shared the GPU for parts of the runs (GPU busy 5-76% before a run). Repeated runs of the same configuration differ by 0.1-0.3 ms; per-feature deltas below 0.15 ms are not meaningful, the tier means over eight scenes are.

## Per-tier cost (merged tree: main `85fbd4fd4`, libultraship `bdce13a4` + this branch)

| Scene | Ultra | High | Medium | Low | Original |
|---|---|---|---|---|---|
| Termina Field noon | 5.41 | 3.26 | 1.84 | 1.28 | 0.34 |
| Termina Field dusk | 5.74 | 3.48 | 2.06 | 1.37 | 0.27 |
| South Clock Town day 1 | 5.61 | 3.44 | 2.26 | 1.71 | 0.34 |
| North Clock Town day 1 | 2.68 | 1.51 | 1.08 | 0.95 | 0.08 |
| Great Bay Coast | 4.91 | 2.62 | 1.80 | 1.36 | 0.28 |
| Stock Pot Inn night | 3.29 | 1.89 | 1.23 | 0.82 | 0.15 |
| Woodfall Temple | 4.39 | 2.74 | 1.59 | 1.18 | 0.14 |
| Ikana Canyon | 4.77 | 3.55 | 2.02 | 1.85 | 0.26 |
| **Mean GPU ms** | **4.60** | **2.81** | **1.74** | **1.32** | **0.23** |
| % of Ultra | 100% | 61% | 38% | 29% | 5% |
| Mean render-thread CPU ms | 3.37 | 3.70 | 2.24 | 2.16 | 1.74 |
| Worst-scene GPU ms | 5.74 | 3.55 | 2.26 | 1.85 | 0.34 |

Ultra is comfortably above 60 fps in every scene: the worst is 5.74 ms GPU (Termina Field dusk), i.e. 10 ms of headroom in a 16.7 ms frame. GL errors: 0 in all 40 runs (`MESA_DEBUG=context`).

### Per stage (mean GPU ms over the eight scenes; "-" = not running)

| Stage (mean over the 8 scenes) | Ultra | High | Medium | Low | Original |
|---|---|---|---|---|---|
| world GI | 0.713 | 0.428 | - | - | - |
| TAA | 0.493 | 0.390 | - | - | - |
| volumetric fog | 0.320 | - | - | - | - |
| screen-space GI | 0.290 | - | - | - | - |
| opaque end / AO | 0.283 | 0.251 | 0.279 | - | - |
| shadows | 0.244 | 0.163 | 0.162 | 0.139 | - |
| volumetric clouds | 0.214 | 0.189 | - | - | - |
| physical sky | 0.168 | 0.145 | 0.167 | 0.167 | - |
| scene end / bloom+grade | 0.158 | 0.139 | 0.195 | 0.195 | - |
| water | 0.128 | 0.099 | 0.058 | 0.054 | - |
| local lights | 0.121 | 0.091 | 0.089 | 0.087 | - |
| grass | 0.121 | 0.054 | 0.027 | 0.007 | - |
| water opaque copy | 0.064 | 0.028 | 0.034 | 0.032 | - |
| sky ambient cube | 0.053 | 0.046 | 0.009 | 0.009 | - |
| ambient particles | 0.044 | 0.030 | 0.033 | - | - |
| emissive surfaces | 0.039 | 0.016 | - | - | - |
| water caustics | 0.030 | - | - | - | - |
| point-light shadows | 0.013 | 0.006 | - | - | - |
| sum of timed stages | 3.50 | 2.08 | 1.06 | 0.69 | 0.00 |
| rest of the frame (world draw, MSAA resolve, UI) | 1.10 | 0.73 | 0.68 | 0.62 | 0.23 |

## Display frame rate with interpolation (RX 7800 XT, GPU ms / achieved fps / render-thread CPU ms)

South Clock Town (the heaviest scene for CPU) and Termina Field dusk, interpolation target 144 and 240 fps:

| Look | SCT 144 | SCT 240 | Field 144 | Field 240 |
|---|---|---|---|---|
| Ultra | 4.3 / 140 / 5.3 | 4.2 / 134 / 6.7 | 5.1 / 141 / 4.5 | 3.1 / 229 / 3.3 |
| High | 3.9 / 136 / 5.6 | 3.3 / 151 / 6.0 | 3.5 / 144 / 3.4 | 2.2 / 217 / 3.5 |
| Medium | 2.2 / 142 / 4.5 | 2.2 / 204 / 3.4 | 1.9 / 144 / 2.3 | 1.9 / 240 / 2.0 |
| Low | 1.9 / 144 / 3.3 | 1.9 / 233 / 3.2 | 1.4 / 144 / 2.1 | 1.4 / 240 / 2.1 |

At 60 fps every look holds 60 with 10+ ms of GPU headroom. At 240 fps the crowded Clock Towns are limited by the render thread's CPU time (Ultra and High about 6-7 ms per frame, 134-151 fps), not by the GPU (about 4 ms); the CPU part includes smooth characters (#109, about +0.4 ms in a crowd) and the effect recorders. Termina Field reaches 229 fps on Ultra. (The GPU ms at 240 is lower than at 144 in the field because the world-GI update is now capped in wall-clock time, see below.)

## Optimisations (all visually unchanged; before/after in `~/dev/ghm-mods/showcase/perf-tiers/`)

| # | Change | Saving (Ultra, 1080p) | Switch |
|---|---|---|---|
| 1 | **Water passes only when water is in view.** The nearest 16 water boxes of a scene were always submitted, so a field with a pond behind the camera ran the opaque colour+depth copy, the wave/ripple/surface/SSR passes and the caustics for nothing. Now the box's surface (plus 300 units of margin for waves and foam) must touch the view pyramid; a submerged camera keeps everything. | water + copy + caustics 0.4-0.5 ms where no water is seen: Termina Field 6.58 -> 5.68 ms, Stock Pot Inn 4.30 -> 3.61 ms (same-session A/B, 2 runs each) | `gNewDawn.Perf.WaterCull` (1) |
| 2 | **World-GI update capped to 30 Hz of wall-clock time.** The radiance and probe update ran every displayed frame, so it cost more at 144 Hz than at 60 Hz. The probes follow the sun and the lamps over seconds; the apply pass still runs every frame. Warm-up after a scene change is unchanged. | world GI 1.24 -> 0.79 ms (Termina, 60 fps), 1.10 -> 0.76 (SCT); three to five times more at 144-240 fps | `gNewDawn.GI.WorldUpdateHz` (30, 0 = old behaviour) |
| 3 | **Half-resolution raw ambient occlusion** with a depth-carrying bilateral blur (the sample's linear depth rides in the green channel, so the blur taps no longer fetch the depth buffer; the raw buffer is a quarter of the pixels). An integer divide in the blur cost 0.3 ms on its own and was replaced by a shift. | AO stage 0.61 -> 0.33 ms (SCT) | `gNewDawn.PostFx.AO.HalfRes` (1) |
| 4 | Overlay/log CPU line (`NewDawnPerf:`) | diagnostics only | `gNewDawn.PostFx.Debug.Stats` |

Evidence: `termina_field_old_vs_new_cull_giHz.png` and `stock_pot_inn_old_vs_new_cull_giHz.png` (left old, right new; the differences between the two are inside the old-versus-old run-to-run difference: RMSE 0.016-0.018 against 0.016 for Termina Field, and in the Inn only Link's idle pose and the performance overlay differ); `ao_fullres_vs_half_3x_crop.png` (3x zoom of Link's feet and the plaza floor, full-resolution AO left, half-resolution right: no halo, no blockiness, no visible difference in the final image); `ao_debugview_full_vs_half.png` (the AO buffer alone: the same occlusion, slightly softer around Link). A first version produced black speckles on silhouettes (a half-resolution texel from another surface got zero blur weight); the blur now falls back to the nearest texel when no tap matches.

### Found but not fixed (other agents' code paths, or too risky for a no-visible-change pass)
- **Water stage has a fixed full-screen cost (0.26 ms + 0.17 ms opaque copy, 0.07 ms caustics) whenever any water surface is in view**, even a small fountain (South Clock Town) or the Woodfall pool. Scissoring those passes to the projected bounds of the visible boxes would save most of it. It is in the water code the shoreline-foam/waterfall work touches, so it was left alone.
- **TAA is four passes (depth/stencil blit, mask, resolve into two targets, sharpen) = 0.49 ms**; merging the sharpen into the resolve or into the bloom prefilter would save one full-screen RGBA16F round trip (~0.1 ms).
- **Volumetric clouds** run at full resolution (0.21-0.35 ms); a half-resolution march with a bilateral upsample is the obvious next step but changes cloud edges.
- **MSAA 4 + RGBA16F** costs 0.1 ms in Clock Town and 0.6 ms in Termina Field (resolve bandwidth); High uses temporal AA without MSAA instead.
- At 240 fps the Clock Towns are render-thread CPU-bound (display-list walk, interpolation, recorders); the GPU is idle half the frame there.

## Tier rebalance
The measured targets were: High about 60% of Ultra, Medium about 35%, Low about 15% with the core look, Original native.

| Tier | Before (same scenes, old build) | Now | Target | What changed |
|---|---|---|---|---|
| Ultra | 5.43 ms | 4.60 ms (100%) | all | nothing removed; optimisations 1-3 |
| High | 4.57 ms (84%) | 2.81 ms (61%) | 60% | 4x MSAA -> temporal AA alone (MSAA off), no volumetric fog, no caustics, no parallax (it already had no screen-space bounce light, world GI tier 1, 3 cascades at 2048) |
| Medium | 2.29 ms (42%) | 1.74 ms (38%) | 35% | no volumetric clouds (everything else as before: 2x MSAA, AO, 2 cascades, foam/refraction, grass, effects) |
| Low | 1.41 ms (26%) | 1.32 ms (29%) | 15% | unchanged; see below |
| Original | 0.25 ms | 0.23 ms | native | unchanged |

(The "before" figures were measured on the tree before this branch; the percentages of Ultra move because Ultra itself got 15% cheaper.)

**Low does not reach 15%.** Ablating Low one feature at a time (South Clock Town, Termina Field, Stock Pot Inn) shows no single removable cost: the linear-light pipeline (HDR targets, tonemap, per-pixel lighting and materials) is 0.4-0.5 ms, the physical sky 0.17-0.2, bloom and grade 0.2, one shadow cascade 0.14, local lights 0.1, water restyle 0.1-0.3 where water is visible, anisotropic filtering 4x about 0.2 in Clock Town, on top of 0.25 ms for the game's own draw. That is the "core New Dawn look" the brief asks Low to keep (tonemap, grade, sky, basic shadows, materials), and it is about 1.3 ms. A 0.7 ms Low would have to drop per-pixel lighting or materials or the physical sky, which is a different look, so it was not done. Low is 29% of Ultra's cost and 1.3 ms in absolute terms (about 750 fps of GPU time on the reference card).

Art direction was not touched: the tier tables only switch features off, the colour grade, exposure, shadow floor and night rows are identical across the four New Dawn tiers, so nights stay dark and moody with no lifted blacks (Low keeps local lights and the lamp pools of the Inn at night).

## Expected cost on other GPUs (estimates, UNVERIFIED)
Only the RX 7800 XT was measured. Scaling the GPU ms by relative raster/bandwidth throughput (a rough rule, shader-heavy stages such as world GI, TAA and fog scale worse on narrow GPUs):

| GPU class (relative to a 7800 XT) | Ultra worst scene (5.7 ms here) | High worst (3.6) | Medium worst (2.3) | Low worst (1.9) |
|---|---|---|---|---|
| RTX 4060 / RX 7600 class (about 0.5x) | ~11-13 ms (75-90 fps) | ~7-8 ms | ~4.5-5 ms | ~4 ms |
| GTX 1660 / RX 580 / Arc A380 class (about 0.25x) | ~23-28 ms (35-45 fps) | ~14-16 ms (60+ fps) | ~9-10 ms | ~7-8 ms |
| Integrated (Vega 8 / Iris Xe, about 0.1x) | not playable | ~35 ms | ~23 ms | ~19 ms (50 fps) |

The first-run default stays Ultra; on a mid-range card pick High, and on an older or integrated one Medium or Low. The CPU side (render thread 3-7 ms per frame here) matters at high refresh rates and on slow CPUs.

## Auto-detecting the look on first run: not added
A quick benchmark that picks Ultra or High was considered and skipped because it would not be reliable:
- On a fresh install there is no 3D scene to time: the first frames are the title screen and menu, whose cost has nothing to do with the cost of Termina Field with grass, GI and clouds.
- The first seconds are dominated by shader compilation, texture upload and the one-time material generation, which would swamp a GPU-time measurement; timing after that means delaying the first look by tens of seconds.
- Whole-frame times are clamped by VSync and the interpolation limiter, and GPU timer queries are per-driver (the readout is only enabled with the overlay); a wrong guess on a laptop with a hybrid GPU (the iGPU renders, the dGPU is named) picks the wrong tier silently.
- A name/VRAM heuristic would misclassify new and old cards equally often, and wrongly picking High costs the player's first impression of the game.
The perf overlay (F3, Esc > Dev Tools) shows GPU ms per stage so players can judge the tier themselves, and the menu cards now state each tier's measured cost.

## Reproducing
`cd ~/dev/ghm-mods/work/perf-tiers && ./matrix.sh <tag> ultra,high,medium,low,original` (each run is `nd-heavy game -- python3 measure.py LOOK SCENE --clock H`; tiers are read from `kRows`, so editing a tier needs no rebuild). A/B of the optimisations on one binary: `gNewDawn.Perf.WaterCull=0`, `gNewDawn.GI.WorldUpdateHz=0.0`, `gNewDawn.PostFx.AO.HalfRes=0`.
