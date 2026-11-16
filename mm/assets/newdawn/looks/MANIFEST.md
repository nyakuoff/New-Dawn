# Look profiles (gNewDawn.Look.Profiles)

One JSON file per profile: what a place looks like by the clock, by light setting, in rain and as the Moon comes down
(`docs/LOOK_PLAN.md`, `docs/LOOK_BIBLE.md`). Field names and ranges are in `libultraship/include/fast/newdawn_look.h`.
Loaded by `mm/2s2h/NewDawn/NewDawnLook.cpp` from `<gNewDawn.Assets.Path>/looks/`; a file of the same profile name in
`gNewDawn.Look.SavePath` (default `newdawn/looks` beside the game, where the look editor saves) replaces the shipped one.

- `outdoor.json`, `interior.json`: the roots, for scenes lit by the clock and for scenes with fixed light settings.
- The others name their scenes and state only what differs from their parent.

Written for this project: New Dawn's own numbers, no third-party content and nothing derived from the game's data.
The first set reproduces the grade, fog and darkening tables that the profiles replace. **CC0 1.0.**
