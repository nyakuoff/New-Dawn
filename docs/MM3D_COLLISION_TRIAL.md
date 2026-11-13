# MM3D collision trial (South Clock Town)

Status: exploratory, opt-in, **not clean**. N64 collision is the default and is untouched.

## What exists
- `gNewDawn.MM3D.Collision` (default 0). When 1 and the scene is on `MM3DCollisionAllowlist.inc` (hand-kept trial
  list, currently `z2_clocktower` only) and `<MM3D.Path>/<scene>/collision.ndcol` exists, the scene's
  `CollisionHeader` is **copied** and only `vtxList`/`polyList`/counts/bounds are replaced
  (`NewDawnMM3D_CollisionOverride`, called from `z_scene_2SH.cpp`). The N64 resource is never modified, so the
  toggle is safe at any time. Surface type table, camera list, water boxes, dynapoly (actor) collision, triggers
  and scene exits stay N64.
- `tools/newdawn/mm3d/collisionpack.py <scene>` builds `collision.ndcol` from the ZSI collision. Each MM3D polygon
  takes the **surface type index and exclusion/conveyor flag bits of the nearest N64 polygon that faces the same
  way** (closest point on triangle, normal dot > 0.6). Nothing from the MM3D surface-type table is used.
- Diagnostics: `gNewDawn.MM3D.CollisionDebug=1` logs Link's position/floor every 20 frames; `=2` raycasts floors
  on a 100-unit grid from y=1400; `=3` raycasts from per-column start heights listed in
  `<MM3D.Path>/probe-start.txt` (`x z y` per line).

## Measurements, South Clock Town (`z2_clocktower`)
Pack: MM3D 651 vertices / 731 polys (floor 207, wall 489, ceiling 35) vs N64 416 / 508 (146 / 335 / 27).
Bounds grow to z = -3187 (N64 -1974): MM3D's town is larger to the north, the 374 columns with an MM3D floor but
no N64 floor are all in that extra area, so those places exist only in MM3D.
Nearest-N64 matching gap: p50 38 units, p90 368; 407 polys (53 % of area) are more than 25 units from any
N64 polygon, so their inherited surface types are a guess.

Floor probe from 60 units above the N64 floor, 374 N64-walkable columns (files in
`~/dev/ghm-mods/mm3d/out/stage-c/collision-trial/`):

| | |
|---|---|
| MM3D has no floor under Link (fall-through) | 36 (9.6 %) |
| within 2 / 5 / 10 / 25 / 50 / 100 units of N64 floor | 65.4 / 70.7 / 71.3 / 74.0 / 79.9 / 94.7 % |
| MM3D floor lower than N64 by more than 10 (Link sinks) | 89 (24 %) |
| MM3D floor higher than N64 by more than 10 (Link floats) | 8 |
| worst difference | 220 units |

Biggest gameplay-relevant differences:
- N64 raised platforms (floor 220 around x 200..300, z 400..600; 180 around x 0, z -300..-200; 100 around
  x -1100..-1000, z 500..600; 200 around x -1500..-1400, z -1400..-700) are absent from MM3D collision at the same
  place, so Link would walk through or under them. These are the places where the town was remodelled.
- Steps and ledges: about 26 % of shared columns differ by more than 25 units, i.e. different stair/ledge heights.
- Walls: MM3D has 489 wall polys against 335; wall positions were not probed (only floors), but the earlier
  Stage A study measured walls within 5 units for the minority of N64 wall area only.
- 53 of the 731 MM3D polys disagree with their matched N64 poly on floor/wall/ceiling class.
- In game: Link spawns and stands correctly at the default spawn (floorY 0, same as N64).

## Verdict
Wholesale MM3D collision for South Clock Town is not viable without also moving N64 actors, NPC paths and
triggers that were authored against the N64 layout. Next steps if pursued: per-region (not per-scene) override
limited to areas where floors agree within 5 units, or walls/ceilings only.
