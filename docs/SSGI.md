# Screen-space global illumination (one-bounce diffuse)

`gNewDawn.GI.Enabled` (preset on; needs `gNewDawn.HDR.Enabled`), `gNewDawn.GI.Strength` (0..2, default 1),
`gNewDawn.GI.Debug=N` (N>0: show only the bounce term x N).

## Design
Runs at the opaque-end stage, right after AO, before fog/particles/water (`RunGlobalIllumination` in
`gfx_opengl_postfx.cpp`). It reads a copy of **this frame's** opaque HDR colour, so bounce never feeds back on itself.
1. **Gather** (quarter-res): 16 golden-angle disk taps within about 140 world units, rotated and radially jittered by
   interleaved gradient noise that is **re-seeded every frame** (`frameIndex & 63`). Positions come from the exact inverse
   projection, normals from depth. A tap counts only if both surfaces face each other and a mid-point depth test shows no
   occluder; each tap's luminance is clamped (keeps hue, kills fireflies). The result is weighted by
   `mix(0.12, 1, 1 - sunVisibility)`: the sun shadow map (when the shadow pass ran this frame) decides whether the receiver
   is in direct sun, so the bounce is mostly a shade effect. Without a sun shadow map (interiors, shadows off) the weight is 1.
2. **Filter**: 5x5 depth-bilateral, then reprojected history (bilinear, each tap rejected by depth error) blended at 0.9,
   clamped to the widened current neighbourhood. History is dropped on scene change, camera cut (>60 units or ~32 degrees
   between frames) or size/aspect change.
3. **Apply**: depth-aware 2x2 upsample, then an **additive** pass (`GL_ONE,GL_ONE`, MSAA samples untouched). With no albedo the
   receiver hue is a bounded proxy of its lit colour; the term fades out on already-bright surfaces (luma 0.5..1.4), is zero on
   crushed blacks, and is capped at 0.9x the receiver luma.

Bug found while verifying: the AO apply pass leaves `GL_BLEND(ZERO, SRC_COLOR)` enabled. GI disables blend and scissor first;
without that every GI target multiplied to zero. The first version also used a fixed per-pixel pattern, which showed as a
halftone stipple in the bounce buffer: per-frame jitter plus accumulation removed it (compare `diff5x-*` in the showcase).

Sampler units: gather uses unit 8 (scene), 9 (depth), 10 (sun shadow map, depth-compare sampler object); the filter uses
10 (raw buffer, sampler object unbound again) and 11 (history); apply 8, 9, 11. All have dummies bound at program creation.

## Evidence (RX 7800 XT, preset + full mods)
| Point | GI ms (live) | Mean luma off -> on (frozen frame 30, 12:00) |
| --- | --- | --- |
| South Clock Town | 0.33 | 89.4 -> 92.7 (+3.8%) |
| Termina Field | 0.27 | 116.4 -> 116.7 (+0.2%) |
| Stock Pot Inn | 0.35 | 66.1 -> 68.5 (+3.6%) |
| Woodfall Temple | 0.31 | 27.7 -> 28.1 (+1.5%) |
| Zora Hall | 0.34 | 77.1 -> 78.3 (+1.5%) |
| Great Bay Coast | 0.28 | 131.8 -> 133.3 (+1.1%) |

0 GL errors at all six points (stats error check) and on the Inn fixture under `MESA_DEBUG=context`. Brightness stays well
inside -10%/+15%. Cost is 0.27-0.35 ms (was 0.56-0.88 ms before quarter-res plus temporal accumulation).

- **Off is byte-identical to main** (previous round, unchanged code path when disabled): frozen Stock Pot Inn, two repeats of
  base and this build with `GI.Enabled=0`, all SHA-256 `440c1ac3...`.
- **Visible effect** (`showcase/gi/split/`, `crop11-*`, `diff5x-*`): red-wall warm light on the Clock Town floor and guard,
  green plant/rug bleed onto the Inn wall, contact glow around Link and under the Field pillar, boat-hull colour on the Coast
  sand. The Coast sand is the weakest of these: it is sunlit and neutral, so the bounce is faint.
- **Temporal, real whip-turn** (`run.py --turn`, Coast, 10 frames 0.2 s apart; the camera spins about 48 mean-luma units per
  frame for four frames, then settles): settled-frame change with GI on is 2.1/2.1/1.1/0.8, identical to GI off
  (2.1/2.0/1.1/0.8). No ghost or crawl visible in `whip-turn-coast-gi-on-frames3-4-5-9.png`.

## Limits
- No albedo, so the receiver colour is the lit colour: a bounce can slightly re-lift an already-coloured wall.
- Only on-screen surfaces contribute (a tall off-screen wall cannot bounce).
- History has no normal rejection (depth only) and re-converges for a few frames after a hard cut.
- Interiors have no sun visibility term, so only the luma gate keeps lit surfaces from being lifted.
- Not verified: pause/pictograph, water scenes with Restyle (water draws after GI), interiors with many point lights beyond the Inn.
