# Water screen-space reflections

Opt-in, OpenGL-only visual pass. `gNewDawn.Water.Reflections` defaults to `0`;
`gNewDawn.Water.ReflectionStrength` defaults to `0.35` (range 0–1). The Water menu
exposes both controls. Reflections also work with Water Glints disabled. They are
not enabled by the New Dawn preset.

## Design

Reuse the existing collision WaterBox plane mask and procedural water slope.
When reflections are enabled, copy opaque colour and depth at OPAQUE_END, after
opaque lighting, before any translucent water/effects. Keep those private targets
separate from the targets reused by shadows, AO and bloom. Only consume a copy
from the same rendered frame and framebuffer size.

At SCENE_END, intersect the view ray with the nearest accepted water plane,
reflect it about a mildly perturbed +Y normal, and march uniformly in screen
space. Reciprocal clip W interpolation supplies perspective-correct ray depth.
The march uses 8–40 steps and five binary refinements. Reject hits below the
plane and fade thickness mismatches, screen edges, long rays and grazing misses.
A depth-aware five-tap colour gather softens hits. Misses fall back to the game's
fog/sky tint. Screen blending, Fresnel and the existing per-scene strength table
keep the addition subtle; poisoned Southern Swamp gets 30% strength.

The disabled path selects the original water shader variant. It allocates no
reflection targets, takes no opaque snapshots and executes no reflection march.
Setting Reflection Strength to zero also skips SSR. The snapshot blits explicitly
disable scissor, including when AO's split debug view was active.

No changes to game logic, collision, animation, timing, saves or assets. The
WaterBoxes are read as visual plane bounds; they are never modified. Neither
copy of `default.shader.glsl` was changed.

## Validation

Build directory in this worktree: `build` (Ninja, RelWithDebInfo).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 12
cmake --build build --target Generate2ShipOtr
```

The authorized local `mm.o2r` was copied from the developer's main build because
the suggested hard link crossed filesystems. CMake dependency fetching and
headless gamescope/GPU access required execution outside the restricted sandbox.
Builds passed with existing unrelated compiler warnings. Both repositories pass
`git diff --check`. Independent renderer review found the scissor issue above;
it was corrected before final validation.

Evidence is local, uncommitted game imagery: `/tmp/nd/water-ssr/`. All scene runs
use the full mod set at `/home/loganp/dev/ghm-mods/sets/full` with
`gEnhancements.Mods.AlternateAssets=1`, Water Glints on at its default strength,
SSR strength 0.35, and default post-fx. Captures are 1920×1080, OpenGL 4.6,
RX 7800 XT, MSAA 1, default 20 FPS, day 1 at 08:00 plus elapsed game time.

Baseline binaries were rebuilt from superproject `5afe9a51b` and libultraship
`b7b7d68e` in this worktree. The main checkout's older existing binary was used
only to scout viewpoints and was excluded from acceptance measurements.

- Deterministic disabled comparison: set the runtime RNG seed to 1 and freeze
  game frame 150 with GDB, without modifying source. `seeded-main/` versus
  `seeded-off/`: **zero differing pixels** across the entire Great Bay frame.
  The scripts and debugger logs remain under the evidence directory.
- Five water views: Great Bay facing the hut (yaw 270), Great Bay cliffs (yaw
  180), Zora Hall pools (default point), South Clock Town canal
  (`--pos=-696,-20,-800 --rot 0`), Southern Swamp bank
  (`--pos=-191,61,-900 --rot 0`). The canal position avoids the scrub dialogue
  trigger and the default points that barely show water.
- Each final view includes eight burst screenshots at requested 0.08-second
  intervals; screenshot readback makes the actual intervals longer. This is a
  stationary-camera stability check, not a moving-camera or transition test.

- Below-plane captures: GDB verified the camera at `(-2000, -70, 4756)` below
  Great Bay's Y=0 plane. No added reflection is apparent. A pixel-identical
  underwater A/B was **not established**: even with a fixed view and the screen
  distortion enhancement disabled, the two captures differ. After three attempts
  this comparison was left inconclusive rather than asserting a complete
  underwater regression proof. Evidence: `below-static-{0,1}/`, debugger scripts
  and logs. The shader rejects every plane at or above the camera before tracing.
- With `gNewDawn.Water.ReflectionStrength=0.0`, GPU logs contain the original
  0.050 ms water pass and no opaque-copy stage. Use a float literal in the capture
  tool: an integer `0` creates an integer cvar, which the float getter does not
  read as the slider's value.

## Measurements and artifacts

Sequential captures, one game process at a time. GPU values are the renderer's
300-run GL timer-query averages in milliseconds; they exclude the existing depth
preparation and CPU work. The added-cost column is `(SSR water + copy) - old water`.

| View | Old water | SSR water | Opaque copy | Added cost |
| --- | ---: | ---: | ---: | ---: |
| Great Bay hut | 0.050 | 0.197 | 0.069 | 0.216 |
| Great Bay cliffs | 0.047 | 0.144 | 0.068 | 0.165 |
| Zora Hall | 0.077 | 0.255 | 0.067 | 0.245 |
| South Clock Town canal | 0.073 | 0.252 | 0.067 | 0.246 |
| Southern Swamp | 0.051 | 0.143 | 0.068 | 0.160 |

Great Bay hut with MSAA 4: old water 0.053 ms, SSR water 0.163 ms,
opaque copy 0.074 ms, added cost 0.184 ms. MSAA 4, AO split debug view, and
SSR with both the main post-fx toggle and Water Glints disabled were exercised
without shader or GL errors. These extra captures are in `msaa4-{off,on}/`,
`split/` and `only/` under the evidence directory.

Raw final captures and logs: `/tmp/nd/water-ssr/final-{off,on}/<view>/`.
Eight-frame bursts were inspected for abrupt streaks/flicker; none were apparent
in these stationary views. Underlying water textures and actors continue to
animate. This does not establish stability for every scene or moving camera.

Before/after crops, each side at exactly 2× native pixels:

- `/tmp/nd/water-ssr/crops/matched-hut-2x.png` (same frame and RNG seed)
- `/tmp/nd/water-ssr/crops/matched-cliffs-2x.png` (same frame and RNG seed)
- `/tmp/nd/water-ssr/crops/pool-2x.png`
- `/tmp/nd/water-ssr/crops/canal-2x.png`
- `/tmp/nd/water-ssr/crops/swamp-2x.png`

`metrics.json` contains GPU readings and raw adjacent-frame differences, which
include the game's own scrolling textures and cannot alone measure SSR flicker.
`reflection-contribution-16x.png` amplifies the matched-frame difference for
inspection; it is a diagnostic image, not the rendered effect's actual strength.

## Limits

Only opaque geometry visible in the current screen can reflect. Thin objects,
objects hidden behind foreground geometry, far geometry, sky and anything outside
the screen use the tint fallback. The bounded march can miss thin silhouettes.
WaterBox/visible-mesh agreement retains the existing water pass's scene limits.
The effect is an added sheen, so dark reflected objects do not produce a full
physical mirror. It intentionally retains the original painted water textures.
Full-resolution tracing was kept because measured cost was small; no temporal
history, half-resolution buffers or extra upsample pass were needed. Gameplay
preservation is supported by the visual-only diff and scene captures, not a full
playthrough or the complete project regression matrix.
