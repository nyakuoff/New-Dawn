# Scene look: engineering plan

**Status:** approved by the maintainer on 2026-11-16 (decisions in section 7). Milestone 2.0 is built; the system as
it exists is described in `docs/LOOK_PROFILES.md`. The target is `docs/LOOK_BIBLE.md`; the evidence is
`docs/LOOK_AUDIT.md`.

## 1. Where the look lives today

About thirty features decide the image, and each carries its own idea of place and time:

| What | Where it is decided today | Keyed by |
| --- | --- | --- |
| Sun, ambient, sky and fog energy | Five global cvars (`gNewDawn.HDR.SunIntensity/AmbientFill/SkyIntensity/FogIntensity/Exposure`), read in about 30 places in the renderer | Nothing: one value for the whole game |
| Light and fog colours | The game's own light settings, passed through unchanged | Scene, clock (native) |
| Shadow density | `Shadows.Strength`, `Shadows.Floor` cvars (together they cap lit:shade at 2.5:1) | Nothing |
| Night and interior darkness | `localDarken` constants in `NewDawnFillLocalLightParams` (0.62 outdoors at night, 0.58 indoors) | Outdoor or not, clock |
| Tone curve | Fixed in the composite shader (identity to 0.76, short shoulder) | Nothing |
| Grade | `sArt*` tables in `NewDawnPostFx.cpp` (14 moods, 4 clock bands) | Scene switch, clock |
| Physical sky | Scene allow-list in `NewDawnSky_Emit` (12 scenes) | Scene switch |
| Sky variants | `ComputeSkyLook` in the renderer (final day, cursed, poisoned, winter) | Variant id |
| Height fog | Scene switch in `NewDawnFillFogParams` (9 outdoor scenes and Woodfall Temple; Ultra only) | Scene switch |
| Screen-space rays | `SunShafts.Strength` cvar | Sun height |
| Bloom | `Bloom.Strength/Threshold` cvars | Nothing |
| World GI gains | Debug cvars | Lamplight or daylight |
| Water palettes and strength | Three tables in `NewDawnPostFx.cpp` | Scene switch |
| Ambient particles | Biome switch in `NewDawnAtmos.cpp` | Scene switch |
| Interior windows, emissive surfaces | A window table in `NewDawnPostFx.cpp`; one switch and one strength for emissive surfaces | Scene; nothing |
| Character light | Its own cvars (`gNewDawn.Characters.*`) | Nothing |

Consequences seen in the audit: the same place is covered by some features and not others (a scene can have the grade
but not the sky, or the sky but not the fog), nothing but the grade knows what region it is in, and the two things
that most define a modern image, the light ratio and the exposure, are constants.

## 2. What is tuning and what has to be built

| Bible requirement | Verdict |
| --- | --- |
| Neutral white at noon, warm key and cool shade | **Build (small):** key and fill need a colour and a level per place and time; today they are the native colours times one scalar |
| Lit:shade of 5:1, shadow density by state | **Build (small):** same plumbing; the values then are tuning |
| Deep, clean nights lit by the Moon and by lamps | **Build (small):** night becomes light levels and exposure instead of a darkening multiply |
| Filmic highlights, authored exposure, eye adaptation | **Build:** new tone curve, exposure per state, auto-exposure |
| Three depth planes in every outdoor frame; haze that follows the sun | **Build:** height fog and aerial perspective tied to the sky, in every outdoor scene and on every tier |
| Shafts in the swamp, the woods, temples, interiors | **Build:** the fog march exists but is fixed to nine scenes and one density; it needs profile inputs, lamp light, and an interior mode beyond Woodfall Temple |
| A sky for every outdoor scene; clouds, haze and stars by state | **Mostly data:** the sky model exists; the allow-list and the variant constants become profile data. Cloud shadows are new and small |
| Lamps, windows and fires as the night's keys | **Mostly data:** local lights, flame light and emissive surfaces exist; their gain, colour and schedule become profile data. Exterior lit windows need a list of surfaces |
| Region palettes | **Tuning,** once the above exists; the grade tables shrink to trims |
| The three-day arc; the Moon as a key on the final night | **Build (small):** a dread modifier on profiles; the Moon already works as the night shadow light |
| Rain, blizzard, dust, ash | **Mostly data:** rain, snow, wind and particle systems exist; they need states and amounts per profile |
| A way to author all of this | **Build:** the profile system and the look editor. Without them every change is a rebuild |

So: one system (profiles and their editor), four pieces of rendering (tone and exposure, light energy, atmosphere,
shafts), and then a long tail of data and tuning.

## 3. Architecture

Evolve, do not rewrite: every existing feature keeps its renderer. What changes is where its numbers come from.

### 3.1 One resolved look per frame

A plain C struct, `NewDawnLookState` (new header `libultraship/include/fast/newdawn_look.h`), of about 70 floats in the
groups of the bible:

- `light`: key colour and level, fill colour and level, ground bounce, shadow density and softness, baked-light level
  (how much of the authored vertex light of an unlit room remains);
- `exposure`: authored EV, adaptation range and speed;
- `tone`: contrast, toe, shoulder, highlight desaturation;
- `grade`: the existing art-grade fields (slope, offset, power, split tone, saturation, green remap), used as trims;
- `air`: visibility, haze height and base, haze tint against the sky's own colour, sun in-scatter and its lobe, mist
  layer, shaft strength;
- `sky`: the existing `SkyLook` fields (haze, deck, glow, cloud layer, coverage, wind), star and Moon-glow levels;
- `bloom`: threshold, strength, tint;
- `practicals`: lamp gain and warmth, flame gain, emissive level, window colour and level;
- `gi`: sun, lamp and baked gains;
- `particles`: biome and amount; `weather`: rain, snow, dust or ash amounts;
- `character`: rim colour and level, set-relative exposure.

The game side fills it once per game frame and copies it into the parameters of every post-fx marker, so it travels
in the display list: interpolated frames and the frozen-frame fixture see exactly the values of the frame they draw.
The renderer keeps a copy for the frame and every consumer reads from it. As built it is a flat array of floats with
one registry row per field, and it grows a milestone at a time: 2.0 carries the 36 fields that existing cvars and
tables already decided.

`gNewDawn.Look.Profiles` (0 until approved, then 1 in every New Dawn tier) selects the source. Off, every feature takes
its numbers from where it takes them today, so off stays byte-identical to main on the frozen-frame fixture and the
existing tables remain the fallback. They are deleted only in the last milestone, when profiles have covered them.

The player's cvars stay, as multipliers on top of the profile: exposure offset, bloom amount, fog amount, saturation,
night brightness. The tiers keep switching techniques on and off and never change look values, as
`docs/PERFORMANCE.md` already requires.

### 3.2 Profiles are data

`mm/assets/newdawn/looks/*.json` (written by us, so shippable; with a `MANIFEST.md`), loaded at start, reloaded on
change in developer builds. A file saved by the editor in the player's folder shadows the shipped one.

```json
{
  "name": "clock_town",
  "inherits": "outdoor",
  "scenes": ["CLOCKTOWER", "TOWN", "ICHIBA", "BACKTOWN", "ALLEY", "OKUJOU"],
  "clock": {
    "6.5":  { "light": { "key": { "kelvin": 3200, "level": 0.45 }, "fill": { "kelvin": 9000, "level": 0.10 } },
              "air": { "visibility": 9000, "mist": 0.3 } },
    "12.0": { "light": { "key": { "kelvin": 5600, "level": 1.0 }, "fill": { "kelvin": 7500, "level": 0.20 } } },
    "23.0": { "light": { "key": { "kelvin": 7500, "level": 0.03 } }, "practicals": { "lampGain": 1.4 } }
  },
  "overcast": { "light": { "key": { "level": 0.2 } }, "air": { "visibility": 3500 } },
  "dread":    { "weight": 1.0 }
}
```

- **Inheritance:** `outdoor`, `interior`, `dungeon` and `cave` bases hold the ladder of the bible; a region profile
  overrides what differs; a scene can override its region. A scene nobody has authored still gets its base, so the whole
  game is covered from the first day and hand work only improves it.
- **Clock-lit scenes** hold keyframes at any hours; every field is interpolated between them on a closed 24-hour curve
  (this generalises the four fixed bands of today's grade).
- **Fixed-light scenes** (interiors, dungeons) hold variants keyed by the game's light-setting index and are blended by
  the game's own `lightBlend`, so a room change cross-fades exactly when the original does.
- **Conditions** select between profiles on read-only facts: scene layer, a named story flag (Woodfall, Snowhead, Great
  Bay or Stone Tower cleared; the Ikana curse value), underwater.
- **Modifiers** are applied in a fixed order after the clock: overcast (from the game's own precipitation), dread (from
  the day and the clock, section 5 of the bible), underwater, cutscene.
- Scene changes through a fade snap; everything else eases with the time constant the grade already uses.

### 3.3 The profile leads, the script is followed

The maintainer's direction is that the original's lighting is not the target, so in ordinary play the profile is
**absolute**: it sets the key, fill and haze colours itself and the game's light colours are ignored. The sun and Moon
directions stay the game's; where the game has no sun at all (cursed Ikana, where the audit found no key and no
shadow at any hour) the profile supplies a fixed key direction.

The game also changes light for reasons the profile cannot know: cutscenes, boss introductions, lightning, rooms that
darken. Those are followed, not overridden:

1. **Actor and event adjustments** (`envCtx.adjLightSettings`, the deltas the game adds on top of its base lights) are
   read directly and applied to the profile's lights as a bounded ratio. Lightning still flashes, a darkened room still
   darkens.
2. **Scripted light settings** (a cutscene or `lightSettingOverride` selecting a light setting the profile has no
   variant for): the resolver latches the ratio between the profile's lights and the game's at the last unscripted
   frame and applies that ratio to whatever the game commands while the script runs. The script's changes arrive
   proportionally, in the profile's world, and the look eases back when it ends.
3. A profile may author a variant for a scripted light setting where the automatic result is not good enough.

<!-- ponytail: the latched ratio is a heuristic; if a cutscene jumps the clock far, the ease-back is visible. The upgrade is a read-only replica of the game's time-of-day light interpolation as the reference. -->

### 3.4 The look editor

A developer window (Dev Tools, next to the performance overlay), built on the existing `GuiWindow` pattern:

- the resolved chain for the current frame: file, keyframes either side of the clock, active modifiers, script mode;
- a slider for every field, editing the keyframe nearest the clock (or adding one), with colour temperature pickers
  for lights;
- preview inputs that only the resolver reads: dread, overcast and story-flag overrides. The clock itself is moved with
  the existing developer time controls;
- hold-to-compare against profiles off, and a vertical wipe;
- scopes: zone false colour, luma histogram, and a two-point probe that prints lit:shade in stops and the red/blue of
  each point (the bible's measurements, live);
- save to JSON with a stable key order, to the player folder or to the repository's asset folder.

## 4. Rendering work

| # | Item | What it is | Tiers | Estimated GPU cost |
| --- | --- | --- | --- | --- |
| R1 | Tone and exposure | A scene-referred filmic curve with profile parameters, replacing the identity-plus-shoulder; it keeps hue the way today's does and adds a toe, a long shoulder and highlight desaturation. Auto-exposure of at most one stop either way from a small log-luminance reduction of the scene (the bloom chain is thresholded, so it cannot be reused). Under the frozen-frame fixture adaptation snaps, so captures stay deterministic | All | 0.03 ms |
| R2 | Light energy | Key and fill become a colour and a level from the profile (the `uHdrSun`/`uHdrAmbient` scalars become vectors); shadow density from the profile; night outdoors is the Moon key plus exposure, and the darkening multiply is retired there; unlit rooms get a baked-light level. The sky-ambient cube, world GI, grass and water read the same values | All | 0 |
| R3 | Atmosphere | Analytic height fog and aerial perspective in the opaque-end pass: closed-form exponential height integral, in-scatter colour taken from the sky radiance cube in the view direction (so haze matches the sky at the horizon and brightens towards the sun without a second model), a forward lobe towards the sun. It replaces the look of the native distance fog while respecting its range (bible section 8). Translucent surfaces use the same colour through their vertex fog | All; today only Ultra has any | 0.05 ms analytic |
| R4 | Shafts | The existing shadowed march, driven by R3's medium; lamp light in the march at night; the interior mode opened to any profile that asks for air; temporal reuse through the TAA history so it can run with fewer steps | Ultra, High at lower resolution | Ultra 0.45 ms (0.32 today), High 0.25 ms |
| R5 | Sky by profile | Every outdoor clock-lit scene is eligible; the variant constants become profile data; coverage, haze and wind keyed by clock, overcast and dread. Cloud shadows on the ground from the coverage map in the shadow pass. The Moon as a red key on the final night through the existing moonlight shadow path | All; clouds as today | +0.02 ms |
| R6 | Practicals | Lamp and flame gain and colour by profile; a schedule and a surface list for lit windows and lanterns outdoors; interior windows coloured by the clock | Ultra to Medium | As today |
| R7 | States | Overcast and dread modifiers; blizzard gusts, dust and ash through the existing wind and particle systems | All | As today |
| R9 | Small effects | Heat shimmer (Ikana), horizon haze by direction (Field), in R3's pass | Ultra to Medium | 0.02 ms |

`default.shader.glsl` changes only for R2 and R3 (two uniforms change type, the fog mix gains a switch); both copies stay
identical and `Generate2ShipOtr` is rebuilt, as the rules require. The MM3D room shader reads the same look.

**Budget** (RX 7800 XT, 1080p, the eight scenes of `docs/PERFORMANCE.md`; worst scene today in brackets): Ultra at most
6.4 ms (5.74), High 3.9 (3.55), Medium 2.4 (2.26), Low 1.95 (1.85). Those brackets are the figures of
`docs/PERFORMANCE.md`. Measured again at milestone 2.0 on main `57a76428e` with `tools/newdawn/lookdev_perf.py`, before
any new rendering, the worst scenes are Ultra 5.70, High 3.98, Medium 2.32 and Low 1.75 ms: High starts above its
ceiling, the others below theirs. Each milestone is therefore judged on its increase over a same-session run with
profiles off as well as on the ceiling. Low and Medium take only R1, R2, the analytic part
of R3 and data; that is where most of the visible change is, so the look holds on every tier. Each milestone re-runs
the performance matrix.

## 5. Phases

Each milestone ends with a build, the performance matrix, a before/after sheet from `lookdev_audit.sh` against the
Phase 1 audit frames, the bible's numbers measured with `lookdev_stats.py`, and the frozen-frame identity check with
profiles off.

| Milestone | Builds | Art direction delivered |
| --- | --- | --- |
| 2.0 Foundation (**done**) | The look array, resolver, JSON loading and inheritance, the editor with sliders and save. Base profiles that reproduce today's look | None: the proof is that profiles drive what the cvars and tables drove |
| 2.1 Light and tone | R1, R2, script following (3.3; it only matters once profiles set colours), editor scopes | **Clock Town and Termina Field**, all clock states and rain: neutral noon, 5:1, golden hour, lamp-lit night |
| 2.2 Air and sky | R3, R4, R5 | Field vistas and horizons, cloud shadows; **Great Bay** |
| 2.3 Regions | R9, data | **Swamp and Woodfall**, then **Snowhead**, then **Ikana and Stone Tower**: one sheet each |
| 2.4 Enclosed places | R6 | **Interiors, dungeons, caves, fountains** |
| 2.5 The three days | R7, the Moon key | **Day 2, day 3, the final night, the Moon**; the cutscene pass |
| 2.6 Clean-up | Delete the superseded tables and cvars; tier re-measurement; documentation | The whole-game sheet |

Work that can be handed to implementation agents once 2.0's header is fixed: the JSON loader and its self-check, the
editor window, the scopes, the capture sheets. Shader and pass changes (R1 to R5) need one owner at a time because they
share the composite pass, the opaque-end pass and the default shader.

## 6. Risks and coordination

- **Albedo.** A real light ratio shows up albedos that carry the original's baked warmth or are too dark or too
  saturated (the audit notes several). The texture pipeline recolours materials to the original's palette; some of
  those palettes include the N64's lighting. Milestone 2.1 will list the offenders for the texture agent rather than
  compensate in the light.
- **Character path.** `Characters.LightNeutral` exists because the scene's light is too coloured; with a neutral noon
  key it over-corrects. The character agent owns that path; this work only supplies `character.rim` and the key and
  fill, and the two are retuned together at 2.1.
- **Cutscenes.** Script following is automatic and unproven. The cutscenes reachable by the fixture (arrival, rooftop,
  Moon approach) are checked at 2.1; the rest need the triggers issue #77 is still looking for.
- **Dark and readable.** Section 8 of the bible sets the limits; the night-brightness multiplier stays for players who
  want more.
- **Scale.** About 110 scenes. Inheritance covers all of them at 2.0; hand authoring goes in the order the player meets
  them.
- **Other tables in flight.** Water palettes, particle biomes and interior windows move into profiles last (2.4-2.6),
  after their owners' current work has merged.

## 7. Decisions (maintainer, 2026-11-16)

1. **Profiles are absolute in ordinary play and follow the game's own scripted lighting** in cutscenes and events (3.3).
2. **Night is dark:** frame median about 0.09 outdoors, lamp and torch pools up to 0.85.
3. **Added weather, visual only:** an overcast day 2 where the game does not rain, and ash on day 3.
4. **Extras in:** the red shadow-casting Moon key on the final night, cloud shadows, lit windows from 17:00,
   direction-coloured haze in the Field, an aurora over Snowhead, phosphorescent surf at Great Bay by night, heat
   shimmer in Ikana. **Out:** film grain and halation (R8 is dropped).
5. **Tier ceilings** of section 4 accepted, with latitude: a clearly better image may take a little more, justified with
   numbers.
