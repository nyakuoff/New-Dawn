# Smooth characters

`gNewDawn.Characters.Smooth` (default 0; on in Ultra and High) and `gNewDawn.Characters.SmoothStrength` (0..1.5,
default 1). Menu: Lighting, "Smooth Characters". Needs per-pixel lighting (the Modern Lighting Pipeline has it) and
OpenGL 4.0 (tessellation). Render only: the game's vertices, matrices, animations, hitboxes and collision are read,
never written.

## What it does
The triangles of Link, NPCs and enemies are drawn as curved surfaces instead of flat ones. Heads, hats, arms, legs and
bodies lose their corners in the silhouette, and light flows across limbs and joints instead of stopping at every
facet. Edges the artists made hard (blades, shield rims, the faces of boxes) stay hard, flat panels stay flat, and
thin parts stay thin.

## Design, and why
**Curved PN triangles through the tessellation stages.** Each actor triangle is a cubic Bezier patch (Vlachos et al.,
"Curved PN Triangles"): it goes through the model's own three points and leaves each along that point's tangent plane.
The point positions never move, so the animation's pose is exactly the game's; only the surface between the points
bulges.

Why hardware tessellation and not CPU subdivision: the interpreter already transforms every vertex on one thread, and
subdividing there would multiply that cost by the patch density. With tessellation the CPU adds two vectors per vertex
and the GPU does the rest, only where the curve is visible: the level of an edge is `sqrt(sag / tolerance)` with the
edge's deviation from its chord measured in pixels, so a straight edge is never split and a distant character is drawn
with its own triangles. Fractional spacing makes the level change without a pop.

Why PN triangles and not Phong tessellation: both need only positions and normals, but the cubic has a control point
per edge end, which is what lets a hard edge be treated on its own (below). Subdivision surfaces (Loop, Catmull-Clark)
would move the model's points and need a closed, clean mesh; these models are neither.

**No cracks, by construction.** A patch edge is fully described by four things: its two end points and one
control-point offset per end. Two triangles sharing an edge show no crack when they hold the same offsets and evaluate
them the same way. So the interpreter computes the offsets from what both triangles have in common, and the stages
evaluate them symmetrically (`precise`, weights written as `(own * own) * other`, an edge's level taken from that
edge alone). The debug check below compares every edge end drawn in a frame: no difference in any capture.

**Adjacency is rebuilt once and cached.** The vertex stream has no adjacency. The first time a triangle command is
seen, the next frame is logged: every character triangle drawn is written down, and at the end of the frame the actors
that drew something new are welded by position (bit-equal world positions: twins at texture seams, hard edges and the
vertices a limb shares with its parent go through the same matrix) and analysed as a whole. Per triangle the result
is, in each vertex's own model space: a welded normal per corner, and per edge end one of
- *normal*: the edge leaves in the tangent plane of the corner's welded normal (the PN rule);
- *crease*: the two triangles on the edge have different normals there (the artist split them), and the edge follows
  the line both tangent planes share. A hard edge then curves along itself (a round rim) and stays hard;
- *straight*: anything else (more than two triangles on the edge, opposite normals, no normal).

Because it is stored in model space it holds for every pose, so later frames only look it up (one hash probe per
triangle, keyed by the command's address, its first vertex and how many matrices the actor had loaded, which tells
apart several draws of one display list). Everything is computed from the model's own integers where the vertices
involved share a matrix, so two actors of the same kind analysed in different poses arrive at the same numbers.

Cases the analysis handles, each found with the crack check or by eye:
- **Normals:** normals within the crease angle (35 degrees, `SmoothCreaseAngle`) at one point are one smooth surface
  and share the mean of the different normals there, each counted once. This is also the shading normal, so shading
  no longer breaks at texture seams or between a limb and its parent.
- **Limb joints:** a limb's display list also loads its parent's matrix for the vertices it shares with it. The shader
  turns every normal of a batch with the batch's matrix, which was wrong for those vertices; smooth draws now hand it
  the normal that comes out right in world space.
- **Overlays:** a triangle lying exactly on an earlier one (the eyes of another blink state, a second pass over a
  face) takes that face's data, whichever frame it first appears in, so both stay one surface.
- **Decals:** decal-mode triangles and the faces under them stay flat.
- **One display list in several actors:** every draw takes part; the one with the most neighbours decides what is
  stored, and where another draw has a neighbour that would need something else, that edge is made straight for all.
- **CPU-skinned meshes** (normals rewritten per frame): the vertex's own normal is used and edges that needed the
  stored neighbour fall back to straight.
- **Commands built per frame:** left alone after four tries; a busy stretch of logged frames is followed by a pause.

**Limits that keep shapes right.**
- *Thin parts.* A bulge grows with the length of its edge, so a spear shaft or a carried beam swelled to several times
  its width. The pull of an edge end is bounded by the height of the narrowest triangle on that edge
  (`SmoothThinLimit`).
- *Flat panels.* An edge that leaves its corner's tangent plane by less than 6 degrees gets no curve, fading in up
  to about 14 (`SmoothFlatAngle`). Without it Link's shield face bulged through the emblem laid just above it.
- *Points.* Past a steepness of 0.8 the pull is mostly held back, so tips are rounded less than they would be
  (`SmoothTipLimit`).

## What counts as a character
Lit triangles (the vertex carries a normal) of a display list called by an actor, under a non-identity matrix, when the
actor loaded two or more matrices after its lights were bound. That is the same test as the character lighting and the
TAA actor mask, except that it is decided when the actor is complete, so its first limb is included. Single-matrix
actors (pots, signs) are props and are drawn as before; `gNewDawn.Characters.SmoothProps=1` includes
them (not evaluated). Rooms, the HUD, sprites and unlit geometry are never touched.

## Integration
- The patches go through the ordinary shaders (two tessellation stages are added between the vertex and fragment
  shader of the same combiner program), so character lighting, the stencil mark for local lights and the TAA actor
  mask, world GI and every screen-space pass see the refined surface with no change of their own (the actor mask was
  captured off and on; the others follow from being per-pixel).
- Shadow maps still take the model's own triangles. The curved surface lies on or outside them, so it does not shadow
  itself; the cast shadow keeps the original outline.
- Texture coordinates are interpolated as before; the surface under them moves by at most the bulge.

## Verification (2026-11-15, RX 7800 XT, Mesa, 1920x1080, Ultra look, community mods unless noted)
Images in `~/dev/ghm-mods/showcase/smooth-characters/`. Off/on pairs are the same frozen frame: game logic is frozen
and the effect is switched by the wall clock (`SmoothBlink`), so pose and camera are identical.

| Subject | File | Result |
| --- | --- | --- |
| Clock Town guard, day | `guard-day_off-on.png` | Shoulders, arms, thigh and helmet rounded; spear shaft unchanged |
| Clock Town guard, night | `guard-night_off-on.png` | Gradients on the thigh and shoulder instead of facet steps; the glint on the boot plate remains |
| Link, 3DS mod, from behind | `link-3ds-back_off-on_2x.png` | Hat and boots rounder; shield face, emblem and rim unchanged |
| Link, 3DS mod, front | `link-3ds-front_off-on_2x.png` | Small change (the model is already dense); face and eyes intact |
| Link, native model, front | `link-n64-front_off-on_2x.png` | Hat, arms, fists, boots clearly rounder; face intact |
| Anju, Stock Pot Inn | `anju-inn_off-on_3x.png` | Hair and ear outline rounder, sleeve facets softer |
| Deku Baba, Termina Field | `deku-baba_off-on_2x.png` | Head and lips rounder |
| Link running and swinging | `animation-burst_off-on-diff.png` | Four frozen moments: same pose off and on, no gaps at joints, textures in place |
| Silhouette, running pose | `silhouette-overlay_run-pose.png` | Actor mask off/on: 23,319 / 24,114 px, overlap 96.5%, 25 px only in off, largest distance 4 px |

- **Cracks:** the debug check found no edge end with differing offsets: South Clock Town 5,043,444 edge ends over 300
  frames, Stock Pot Inn 4,609,170, and none in the Termina Field captures.
- **Off identity:** with `gNewDawn.Characters.Smooth=0` this build and main `e4a9683fb` give the same file for the
  frozen Stock Pot Inn (clock 12, frame 30), SHA-256 `705ef49b138dda5f...9c6d1105`, two runs each. The rules' off list
  no longer makes that fixture deterministic on main; it also needs `gNewDawn.TAA.Enabled=0`, `Vfx.Enabled=0`,
  `GI.World=0`, `Lighting.FlameLight=0`, `Interior.Emissive=0`, `Grass.Enabled=0` and `Foliage.Light=0`.
- **GL errors:** none under `MESA_DEBUG=context` in any capture.
- **Performance, South Clock Town day 1** (guard, three carpenters, Link; 2,801 patches per frame): world draw batches
  0.837 -> 0.853 ms GPU (+0.016 ms). CPU: 0.40 ms per frame for building the patch data, timers included (about
  140 ns per patch); the lookup of culled triangles is not in that figure. The one-off analysis when a scene's
  characters are first seen took about 5 ms (14,322 triangles logged, 2,443 analysed); later arrivals re-analyse only
  the actors they belong to. The cache held 13,605 entries there (112 bytes each).

## Not verified, and limits
- Deku, Goron, Zora and Fierce Deity Link, the Postman, North Clock Town and a torch-lit close-up were not captured.
- No Chuchu was captured. An enemy that draws with a single matrix counts as a prop and is unchanged.
- Models whose limbs are flat-shaded by split normals keep their facets: those are creases by the rule above.
- A part that appears next to an already cached neighbour with different normals at their shared points is handled
  when both are seen together; a third variant meeting the same neighbour with other normals could differ until it
  is re-analysed. Not observed.
- Cast shadows keep the original outline. OpenGL only; other backends ignore the setting.

## Debug settings
`gNewDawn.Characters.SmoothDebug` 1: timing and the crack check in the log (`SMOOTHDBG`), 2: timing only.
`SmoothBlink` (seconds, float): the effect is on only in the odd spans of the wall clock. `SmoothTolerance` (pixels,
0.35), `SmoothMaxLevel` (8), `SmoothCreaseAngle`, `SmoothFlatAngle`, `SmoothThinLimit`, `SmoothTipLimit`,
`SmoothProps`.

## Files
`libultraship`: `src/fast/interpreter.cpp` (`Smooth*`), `include/fast/interpreter.h`,
`src/fast/backends/gfx_opengl.cpp` (`BuildSmoothStages`), `src/fast/shaders/opengl/default.shader.glsl` (two vertex
attributes; `mm/assets/custom/shaders/opengl/default.shader.glsl` is the packaged copy). Menu and looks:
`mm/2s2h/BenGui/BenMenu.cpp`, `NewDawnPresets.cpp`.
