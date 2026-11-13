# Wind and foliage sway (A3, experimental)

`gNewDawn.Atmos.Wind` (default **0**, not in the preset; menu "Wind: Foliage Sway and Rain Slant (HDR)").
`gNewDawn.Atmos.WindScale` (debug multiplier, default 1; 10 was used to prove the effect in captures).

- One wind (`libultraship/include/fast/newdawn_wind.h`): fixed direction matching the cloud wind, gust 0.3–1.6
  from slow swells plus flutter, wall-clock only (no game time, RNG or state).
- Sway: `NewDawnHDR_WindScope` markers (`z_room.c` `Room_Draw`) bracket room/scene geometry in both display
  lists. Inside the scope the interpreter displaces cut-out (`CVG_X_ALPHA`, same test as the foliage-shadow
  classification) triangles on a **copy** of the loaded vertices; the world position and the clip position are
  displaced consistently (clip delta via the projection matrix). Weight is `(1 - t)^2` of the cut-out texture's v,
  so tips move and bases stay planted. The foliage-shadow recorder reads the same displaced copy, so shadows follow.
  Loaded vertices, collision, animation and actors are never modified.
- Rain slant: the new rain streaks use the gusting wind vector (slant only; drop positions use fixed drift).

Evidence (2026-11-13, MESA_DEBUG=context, 0 GL errors, all three atmosphere cvars on): Woodfall exterior plant and
reeds (and their water reflections) move; at scale 1 the displacement is a few pixels (burst crop), at scale 10
(debug) clearly visible; a diff mask confirms only foliage and water reflections change. Splits:
`~/dev/ghm-mods/showcase/atmos/a3/`.

Limits: only room/scene geometry sways; foliage that is an *actor* (grass tufts, bushes, EN_WOOD02 trees) is not in
scope yet (a whitelist of actor draws would be needed); v-weighting assumes the cut-out tip is at the top of the
texture; no CPU cost measurement (per-triangle work only inside the scope); exact-main off-path identity not
proven (same open fixture gate as A1/A2); clouds/particles still use their own constant drift.
