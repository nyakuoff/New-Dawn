# Look profiles

The system behind the scene look rework (`docs/LOOK_PLAN.md`, target in `docs/LOOK_BIBLE.md`). A **profile** is a JSON
file that says what a place looks like by the clock, by light setting, in rain and as the Moon comes down. Each game
frame the game resolves the profiles that apply into one **look**, a flat array of numbers, and the renderer's features
take their numbers from it instead of from one cvar or a built-in table.

- `gNewDawn.Look.Profiles`: default **0**; **1** in the Ultra, High, Medium and Low looks. Needs the Modern Lighting
  Pipeline (`gNewDawn.HDR.Enabled`). Menu: Settings > Advanced, above Art Direction.
- Off, or when no profile file can be loaded, every feature uses its cvar or table exactly as before.
- Visual only. The resolver reads the scene, the light mode and light setting, the clock, the day, the precipitation
  counters and four story flags. It writes nothing.

## Fields

Defined once, in `libultraship/include/fast/newdawn_look.h` (name, default, range, the player's cvar for the same
quantity). The loader, the resolver, the editor and the saved files all come from that table, so a new field is one row
plus the code that uses it.

| Field | Default | Used by | Player's cvar on top |
| --- | ---: | --- | --- |
| `light.keyLevel` | 1.2 | Sun (and Moon) strength on world, actors, grass, foliage, water, world GI | `HDR.SunIntensity` |
| `light.fillLevel` | 0.72 | Ambient strength | `HDR.AmbientFill` |
| `light.shadowStrength`, `light.shadowFloor` | 0.85, 0.30 | Sun and Moon shadows | `Shadows.Strength`, `Shadows.Floor` |
| `light.darken` | 0 | Darkness of the unlit scene at night and indoors (local-light pass) | `Lighting.LocalContrast` (as before) |
| `practicals.lampGain` | 1 | Gain of lamps and flames | `Lighting.LocalStrength` (as before) |
| `exposure.ev` | 0.05 | Exposure before the tone curve | `HDR.Exposure` (a shift, in EV) |
| `tone.contrast`, `tone.saturation` | 1.08, 1.12 | The global display grade | `PostFx.Grade.*` |
| `grade.*` (15 fields) | identity | The art-direction grade: gain, warmth, tint, lift, gamma, contrast, saturation, shadow and highlight tint, green shift and saturation | `Grade.ArtStrength` (as before) |
| `air.fogLevel` | 0.85 | Fog colour level | `HDR.FogIntensity` |
| `air.fogDensity`, `air.fogHeight`, `air.fogBase` | 0, 700, 0 | Volumetric height fog; density 0 means the scene has none | none |
| `air.rays` | 0.30 | Sun shafts and screen-space rays | `SunShafts.Strength` |
| `sky.level`, `sky.coverage` | 0.85, 0.55 | Sky brightness, cloud coverage | `HDR.SkyIntensity`, `Sky.Coverage` |
| `bloom.strength`, `bloom.threshold` | 0.18, 1.0 | Bloom | `PostFx.Bloom.*` |
| `gi.sunGain`, `gi.lampGain`, `gi.bakedGain` | 0.5, 3, 0.6 | World GI bounce gains | `GI.World*Gain` |

**The player's cvar stays a control on top.** The look's value is multiplied by (cvar / default), or shifted by
(cvar - default) for exposure. The defaults are the values of the New Dawn looks, so with those a profile that states
nothing gives exactly what the cvars alone give.

Not in the look yet: light and haze *colours* (still the game's own, milestone 2.1), the tone curve, the sky's variant
constants, water palettes, particle biomes, interior windows.

## Profile files

Shipped in `mm/assets/newdawn/looks/` (copied next to the game with the other New Dawn assets) and read from
`<gNewDawn.Assets.Path>/looks/`. A file with the same profile name in `gNewDawn.Look.SavePath` (default `newdawn/looks`
beside the game; where the editor saves) replaces the shipped one.

```json
{
  "name": "clock_town_streets",
  "inherits": "clock_town",
  "scenes": ["CLOCKTOWER", "TOWN", "ICHIBA", { "scene": "21MITURINMAE", "when": "!woodfall_cleared" }],
  "base":   { "air": { "fogDensity": 0.35, "fogHeight": 700 } },
  "clock":  { "6": { "grade": { "warmth": 0.25 } }, "12": { "grade.warmth": 0.0 } },
  "lights": { "3": { "light": { "darken": 0.4 } } },
  "overcast": { "light.keyLevel": 0.3 },
  "dread":    { "grade.saturation": 0.8 }
}
```

- **Scenes** are named by their enum name without `SCENE_`. An entry may carry a condition on a story flag
  (`woodfall_cleared`, `snowhead_cleared`, `greatbay_cleared`, `stonetower_cleared`; `!` negates).
- **Which profile applies:** of the profiles that list the scene and whose condition holds, the one with the longest
  chain of parents (the most specific). Its parents apply beneath it. Where `inherits` is absent the chain ends in a
  root chosen by the scene's kind of light: `outdoor` for scenes lit by the clock, `interior` for scenes with fixed
  light settings. A scene nobody lists gets its root alone.
- **`base`:** constant values. A child's value replaces its parent's.
- **`clock`** (scenes lit by the clock): values at hours of the day. Each field has its own track, made of every key
  in the chain that states it; a child's key replaces its parent's key at the same hour. Between keys the value moves
  on a smoothstep, around midnight too. A field with a track ignores `base`.
- **`lights`** (scenes with fixed light settings): values per light-setting index, blended exactly as the game blends
  between its previous and current setting. Such scenes ignore `clock`.
- **`overcast`, `dread`:** where full rain or snow and full dread move the look; applied in that order after the above,
  in proportion. Overcast is the game's own precipitation (rain / 40 + snow / 128, clamped). Dread follows the bible's
  three-day table (`Dread()` in `NewDawnLook.cpp`). No shipped profile uses either yet.
- Nested objects and dotted names are the same thing: `{"light": {"keyLevel": 1}}` and `{"light.keyLevel": 1}`.
- Unknown fields, scenes and malformed keys are logged and skipped; values are clamped to the field's range.

The look eases towards its target by 6% per game frame (the time constant the art-direction grade used) and starts on
its target after a scene change.

## How it reaches the renderer

`NewDawn::Look::Resolve` runs once per game frame. `NewDawnPostFx_AllocParams` copies the result into every
`NewDawnPostFxParams` (`lookOn`, `look[]`), so the look is part of the display list: interpolated frames and the
frozen-frame fixture draw with the values of their own frame. The interpreter publishes it at each post-fx marker
(`Fast::NewDawnLookNow()`), and the renderer reads a quantity with `NewDawnLookValue(look, field, cvarValue)`.
Game-side parameters (grade, fog, darkening, shadow and ray strength) are filled from the same resolved look.

## The look editor

Dev Tools > General > Look Editor (`gWindows.LookEditor`).

- **This frame:** the scene, whether it is lit by the clock or by a light setting, the hour, and the overcast and
  dread amounts, each with a preview override that only the resolver reads (`gNewDawn.Look.PreviewOvercast`,
  `.PreviewDread`; -1 follows the game). Move the clock itself with the save editor's time controls.
- **Profile:** the chain that applies, root first; pick which one to edit.
- **Block:** base, a clock key (any half hour; buttons jump to the keys the chain already has), a light setting,
  overcast or dread.
- **Fields:** one slider per field. A tick means the chosen block states the field; moving a slider states it, clearing
  the tick removes it. "now" is the value in use this frame.
- **Save** writes the edited profile to `gNewDawn.Look.SavePath`. To edit the shipped set in a checkout, point that
  cvar at `mm/assets/newdawn/looks`. **Reload files** drops unsaved changes.

## Self-check

`gNewDawn.Look.SelfCheck=1` logs one line at the first frame of a scene:

1. the resolver on built-in data: keys, wrap past midnight, key merging with a parent, inheritance, the most specific
   profile, conditions, modifiers, light-setting blends, a save and read back, the dread curve;
2. the shipped profiles against the tables they stand in for: for every scene, both kinds of light, both Woodfall
   states and every quarter hour, the grade, the darkening, the lamp gain and the fog numbers must match.

## Validation of milestone 2.0 (2026-11-16)

Linux, Mesa OpenGL 4.6, RX 7800 XT, 1920x1080, the Ultra look, community mods set, every game through `nd-heavy`.
Base: main `57a76428e`. All captures ran with `MESA_DEBUG=context` and `gNewDawn.PostFx.Debug.Stats=1`: **0 GL error
lines** in every log (the 61-frame matrix, the identity runs, the demonstration, the editor). Images are local, in
`~/dev/ghm-mods/showcase/lookdev/m20/`.

**The shipped profiles reproduce the tables.** `gNewDawn.Look.SelfCheck=1`: `ok (43392 cases, worst difference from the
tables 0.0000001)`. A first run failed on the inverted Stone Tower at night, where the table reaches a warmth of -1.3 and
the field's range stopped at -1; the grade ranges were widened.

**Profiles off is the old path.** Frozen Stock Pot Inn (`--freeze 30`, clock 12:00, `--wait 25`): one SHA-256,
`0a902063737469820517e50fb65f8521d8e119bf6957440a3a0aaf6cf1fa528a`, for two runs of a build of main (the play build at
`57a76428e`), six runs of this build with profiles off, and five runs with profiles on (the last pair on the final
binary).

**Profiles on matches profiles off outdoors too**, on frozen frames of this build (`--freeze 60`):

| View | Noon | Night | Dusk (17:30) |
| --- | --- | --- | --- |
| South Clock Town plaza | identical file | identical file | 8.5% of pixels one code value apart, 77 pixels more (largest 11) |
| Woodfall Temple | identical file | identical file | not run |
| Ikana Canyon | identical file | one pixel, one code value apart | not run |
| Termina Field | identical file when the same fixture outcome lands (below) | one pixel, one code value apart | as the plaza, plus the fixture's variation |

The dusk difference is real and tiny. While the clock ramps the night darkening in, the value is smoothed twice with
profiles on (once by the look, once by the local-light code as before), so it trails by about 0.8 s more: at 17:30 the
frame is 0.0001 brighter in its median. It vanishes once the ramp ends.

**The whole audit matrix, profiles on, against the Phase 1 frames** (taken on main `d241464c4`, before this work and
before #130): of the 59 frozen frames, 14 are identical files; the other 45 differ in pixels (RMSE at most 0.0050 of
full scale) and not in tone: median luma within 0.0003 and mean saturation within 0.0001 on every frame. The two live
rain frames are within 0.0042 of their medians. Table: `compare_audit_vs_after.txt`; sheets: `after/sheets/` (the
full-size frames were not kept: they repeat the audit's).

**A profile file changes one place and nothing else.** With
`gNewDawn.Look.SavePath` pointing at a folder holding one file, an override of `clock_town_streets` (key 1.6, fill
0.42, shadows at full strength with a floor of 0.10, exposure -0.15, the day grade's warmth and highlight tint
removed), and no rebuild: East Clock Town at noon goes from median 0.413, saturation 0.563 and a shade red/blue of 2.37
to 0.311, 0.383 and 1.40, and its cast shadows become dark; South Clock Town changes likewise; Termina Field is the
identical file. `demo/` holds the pairs and the override. This is a demonstration of control, not the art direction
of milestone 2.1.

**The editor** opens from the menu or with `gWindows.LookEditor=1`: `editor/east_clock_town.png` shows it at 17:56 in
East Clock Town on the chain `outdoor > clock_town > clock_town_streets`, on the 17.5 key. Its sliders and its save
button were not operated in a capture (no input in the headless runs); the save path itself is exercised by the
self-check, which writes a profile into the save folder and reads it back.

**GPU cost: none measurable.** Whole-frame GPU ms from `tools/newdawn/lookdev_perf.py` (the eight scenes of
`docs/PERFORMANCE.md`, one 40 s live run per cell, 60 fps interpolation, vsync off; mean / worst scene):

| Look | Profiles off | Profiles on |
| --- | ---: | ---: |
| Ultra | 4.91 / 5.70 | 4.92 / 5.73 |
| High | 3.34 / 3.98 | 3.31 / 3.91 |
| Medium | 1.94 / 2.32 | 1.93 / 2.29 |
| Low | 1.38 / 1.75 | 1.37 / 1.73 |

The differences are inside the run-to-run noise of the method (0.1-0.3 ms per cell). The profiles add no pass and no
texture; they change where the numbers of 36 fields come from. These figures are also the baseline of today's main for the
milestones that follow: note that High already measures 3.98 ms in South Clock Town, above the 3.9 ms ceiling that
`docs/LOOK_PLAN.md` derived from the older table in `docs/PERFORMANCE.md`.

### Two findings about the capture fixtures

- **The frozen-frame fixture is not fully repeatable outdoors.** Termina Field at noon gave three different files in
  ten runs, and Ikana Canyon two in four, with profiles off as well as on. The runs differ in speckle on grass and
  foliage edges (RMSE 0.0036, tone unchanged). Byte identity is therefore a usable test in the Inn, the plaza and the
  temple, and a statistical one in those two views.
- **A live capture 18 s after launch can land inside the day's opening fade** (the rainy plaza came out with a mean of
  0.29 and 0.42 in two runs with profiles off, against 0.23 once the fade is over). `lookdev_audit.sh` now waits 30 s for
  its rain frames.

### Limits

- Colours of light and haze, the tone curve and script following are milestone 2.1; until then profiles can change
  levels, exposure, shadows, fog, bloom and the grade, and inherit the game's light colours.
- Fog in a scene with fixed light settings still needs the Woodfall Temple path; a profile cannot yet give other
  interiors a medium.
- The tables in `NewDawnPostFx.cpp` remain as the path for profiles off and as the reference of the self-check. They are
  removed in milestone 2.6.
