# Screen-space global illumination (one-bounce diffuse)

`gNewDawn.GI.Enabled` (preset on; needs `gNewDawn.HDR.Enabled`), `gNewDawn.GI.Strength` (0..2, default 1),
`gNewDawn.GI.Debug=N` (N>0: show only the bounce term x N).

## Design
Runs at the opaque-end stage, right after AO, before fog/particles/water (`RunGlobalIllumination` in
`gfx_opengl_postfx.cpp`). It reads a copy of **this frame's** opaque HDR colour, so bounce never feeds back on itself.
1. **Gather** (half-res): 16 fixed golden-angle disk taps within about 140 world units. Positions come from the exact
   inverse projection, normals from depth (full 4-tap for the receiver, one-sided for emitters). A tap counts only if both
   surfaces face each other (receiver and emitter hemisphere) and a mid-point depth test shows no occluder. Each tap's
   luminance is clamped before gathering (keeps hue, kills fireflies).
2. **Filter**: 5x5 depth-bilateral, then a reprojected previous-frame history blended at 0.6, clamped to the current
   neighbourhood and rejected on depth error, scene change, camera cut (>60 units or <~32 degrees turn) or size/aspect change.
3. **Apply**: depth-aware upsample, then an **additive** pass (`GL_ONE,GL_ONE`, so MSAA samples are untouched). With no albedo
   available the receiver hue is a bounded proxy of its lit colour; the term fades out on already-bright surfaces
   (luma 0.35..1.1), is zero on crushed blacks, and is capped at 0.6x the receiver luma.

Bug found while verifying: the AO apply pass leaves `GL_BLEND(ZERO, SRC_COLOR)` enabled. GI therefore disables blend and
scissor first; without that every GI target multiplied to zero and a resolved-colour write squared the frame.

## Evidence (frozen frame 30, 12:00, HDR, preset + full mods, RX 7800 XT)
| Point | GI ms | Mean luma off -> on |
| --- | --- | --- |
| South Clock Town | 0.77 | 89.4 -> 92.3 (+3.3%) |
| Termina Field | 0.56 | 116.4 -> 116.8 (+0.3%) |
| Stock Pot Inn | 0.88 | 66.1 -> 67.8 (+2.6%) |
| Woodfall Temple | 0.70 | 27.7 -> 28.0 (+1.0%) |
| Zora Hall | 0.83 | 77.1 -> 78.0 (+1.1%) |
| Great Bay Coast | 0.62 | 131.8 -> 133.2 (+1.0%) |

GPU ms are from the live (non-frozen) preset captures (`Debug.Stats=1`, 300-frame averages); 0 GL errors everywhere
(`MESA_DEBUG=context` on the Inn fixture, plus the stats error check at all six points). The Inn (0.88 ms) and Zora
Hall (0.83 ms) slightly exceed the 0.8 ms budget, the rest are inside it. Brightness stays within -10%/+15%.

- **Off is byte-identical to main**: frozen Stock Pot Inn, two repeats each of the base binary and this build with
  `GI.Enabled=0`, all four screenshots SHA-256 `440c1ac3...` (the fixture hash differs from older docs because
  the preset/mods moved on since; main and ours agree).
- **Visible effect**: warm bounce on shaded floor beside the red Clock Town wall; green glow from the Inn plants and
  rug onto the wall; contact glow around Link and under the Field pillar; lit hull colour on the Coast sand.
  `showcase/gi/split/*.png` (left off, right on, same frozen frame), `showcase/gi/diff5x-*.png` (difference x5),
  `showcase/gi/crop-inn-wall-1to1-*.png`.
- **Temporal**: an 8-frame, 0.2 s-spaced burst on Termina Field while the camera moves: frame-to-frame mean change
  is 1.6-2.7 with GI on and 1.9-3.3 with GI off, with no ghost trail visible (`burst-termina-frames3-4.png`).
  It is a drifting camera, not a hard whip-turn.

## Limits
- No albedo, so the receiver colour is the lit colour: a "bounce" can slightly re-lift an already-coloured wall.
- Only on-screen surfaces contribute (a tall off-screen wall cannot bounce). Actors' contact glow shows mild grain at 5x
  amplification, not at 1:1.
- Fixed per-pixel sample pattern: static noise is smoothed spatially, history only damps motion shimmer.
- Not verified: pause/pictograph, water scenes with Restyle (water draws after GI), interiors with many point lights beyond
  the Inn, a hard camera whip-turn, the 1.2 ms first-draft cost is gone but 0.8 ms is slightly exceeded in two scenes.
