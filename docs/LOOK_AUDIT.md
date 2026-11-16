# Look audit: the Ultra look, place by place

**Date:** 2026-11-16. **Build:** main `d241464c4`, libultraship `73bfedeb` (my own build of it in this worktree).
**Purpose:** Phase 1 of the scene look rework. It is the "before" for everything `docs/LOOK_BIBLE.md` proposes.

## How it was captured

```
tools/newdawn/lookdev_audit.sh ~/dev/ghm-mods/showcase/lookdev/audit
tools/newdawn/lookdev_stats.py ~/dev/ghm-mods/showcase/lookdev/audit
```

- 61 frames, 1920x1080, RX 7800 XT, the Ultra preset, community mods set, each through `nd-heavy game`.
- Every frame except the two rain frames is a frozen frame (`capture.py --freeze 60 --wait 24`), so re-running the
  script on the same build reproduces it and running it on a later build is a fair before/after. Rain is live, because
  rain needs the game running.
- Clock states: dawn 06:30, noon 12:00, dusk 17:30, night 23:00 on day 1; rain at 12:00 on day 2; the final day at
  12:00 and at 03:00 (the last hours).
- No GL or shader error line in any of the 61 capture logs. They were captured without `MESA_DEBUG=context`; no
  renderer code was changed for this audit.
- Images are local only: frames `<place>_<state>.png`, one sheet per place in `sheets/`, numbers in `stats.txt`.
- New capture points for this: `south_clock_town_plaza`, `east_clock_town`, `snowhead`, `grotto`,
  `great_bay_coast_sea`.

Every frame was looked at, the noon Clock Town frame and two details at 1:1. The contact sheets posterise when
downscaled; nothing below is a claim about banding.

**Columns:** median and range are the frame's median display luma and the stops between its 5th and 95th percentile;
sat is mean saturation; split is the red/blue of the bright quarter over that of the dark quarter (above 1: warm
light, cool shade).

## Verdict

The pieces are good and the picture is not. Materials, clouds, shadows, stars and the few real light pools hold up.
What reads old is everything that should tie them together: **the light has no ratio, the shade has no colour of its
own, the air is empty, and time of day is a tint.** Most frames are evenly lit geometry under a colour wash: orange
for dawn and dusk, blue for night, mustard for the final day. That is the N64's lighting model, rendered sharply.

## Findings across the whole set

1. **Time of day is a colour filter.** Dawn and dusk are the same orange wash, night is one blue wash, the final day
   one mustard wash; the structure of the light does not change. South Clock Town at dawn and at dusk: saturation 0.64
   and 0.69, and the *shade* has a red/blue of 2.0 and 2.8, so shadows are orange too. The cleared swamp's grass turns
   orange at dusk (saturation 0.79). Mountain Village at night is ultramarine paint (saturation 0.70).
2. **White is never white.** The white stall canvas in South Clock Town at noon measures red/blue 3.4 in linear light.
   Snow in the noon sun measures 0.78. North, East and West Clock Town at noon have a mean saturation of 0.56 to 0.75.
   The warmth is built into "day": the Clock Town grade has a base warmth of 0.40, on top of the game's own warm
   ambient.
3. **Shade has the light's hue, or a warmer one.** Split is at or below 1 in East and West Clock Town at noon (0.82,
   0.97), in the rain (0.89), in the swamp at dawn (0.83) and in the Field on the final day (0.83).
4. **Shadows cannot be dark, and many surfaces have no key at all.** With `Shadows.Floor` 0.30 and `Shadows.Strength`
   0.85 a cast sun shadow keeps at least 40% of the lit value, so lit:shade cannot exceed 2.5:1 (1.3 stops). Measured
   on one material: 2.3:1 on snow under the trees at noon; 3.4:1 on paving under the Moon, whose pass adds a lift on
   the lit side. At noon the game's sun is straight overhead, so walls are lit by ambient only. In cursed Ikana the
   game disables its sun: no key and no shadow at any hour.
5. **Exposure is one constant, so brightness is accidental.** Frame medians run from 0.05 to 0.78 without intent.
   Mountain Village at dawn (0.57) is brighter than Clock Town at noon (0.48). East Clock Town at 23:00 (0.18) is
   brighter than the inside of the Inn (0.12). Stone Tower sits at 0.05-0.12 with 11-21% of its pixels pure black.
6. **The air is empty.** Apart from the horizon band at the coast, no frame has aerial perspective: the Field's tree
   line and far wall are as crisp and dark as the foreground. The swamp has no mist, Snowhead has no blizzard (the far
   peaks are clear), Ikana has no dust. No frame of the 61 shows a light shaft, other than the grotto's native beam
   polygon. Volumetric fog is on in this preset; in these views it is invisible.
7. **Sky and ground disagree.** Coast at dawn: a bright orange sky over dark maroon sand that receives none of it, and
   the horizon glows in the west at sunrise. Field on day 2: the ground is dimmed for rain under a fair sky with blue
   gaps, and no rain falls at that spot. The last hours: a red storm sky over calm blue moonlit ground, in Clock Town
   and in the Field. The dread is in the sky only.
8. **Night is a tinted day.** East Clock Town at 23:00 is its noon frame multiplied by violet: every wall evenly
   readable, no lit window, no lamp. The two night frames with a real source (the torch in South Clock Town, the spirit
   flame in Ikana) are the best night frames in the set.
9. **Interiors ignore the clock and their own lamps.** The Stock Pot Inn at 12:00 and at 23:00 is byte-identical. It is
   dim, greenish and flat; the doorway and the lamps glow and shape nothing. Great Bay Temple's lamps are bright discs
   on an olive wall. Woodfall Temple (median 0.07) is murk, not darkness with shape.
10. **Saturation stands in for richness.** Zora Hall 0.75, Stone Tower 0.70-0.78, Stone Tower Temple 0.71. Ikana is
    the reverse (0.19-0.41), but it is grey rather than bleached: its daytime highlights reach only 0.57-0.82.
11. **Highlights stop short.** Sunlit ground tops out at 0.80-0.84 (Clock Town, Field, coast at noon) and nothing
    reads as brilliant; then snow goes to 0.97. The tone curve is the identity up to 0.76.
12. **Some effects read as stickers.** The night fireflies in the Field and the swamp are flat yellow-green discs up to
    the size of Link's head. The grotto's light pool is a white blob. Rain puddles are flat sky-coloured sheets.

## What already works and should be kept

- The materials at 1:1: paving, sand, snow, grass tufts.
- The cloudscapes at noon and at dusk; the stars and Milky Way over East Clock Town.
- Coast towards the sea at dusk: sun disc, a glitter path on the water. The best frame in the set.
- Tree shadows on snow at noon, with properly blue shade (red/blue 0.49 against 0.78 in the light).
- The Moon's shadow across the plaza and the torch's pool in South Clock Town at night; the spirit flame's pool in
  Ikana.
- The dappled shadow on the mossy wall in South Clock Town at noon.

## Place by place

### South Clock Town (plaza)

| State | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Dawn | 0.23 | 5.0 | 0.64 | 1.61 | Rust-orange paving with orange glints under an orange sky. Reads as a sunset through a tobacco filter, not as morning. No cool shade anywhere. |
| Noon | 0.48 | 5.9 | 0.61 | 1.38 | Paving is a uniform saturated yellow-orange; the white canvas is cream. No cast shadow reaches the ground except a blob under Link. The mossy wall's dappled shadow is the one lit moment. |
| Dusk | 0.19 | 4.2 | 0.69 | 1.32 | The dawn frame again, a little darker. Dawn and dusk cannot be told apart. |
| Night | 0.11 | 5.4 | 0.47 | 1.71 | The best of the seven: a Moon shadow across the plaza and one torch with a pool. Everything else is one blue; no window is lit; the NPC is brighter than anything around him. |
| Rain | 0.21 | 5.1 | 0.63 | 0.89 | Brown and dim. An overcast noon that is still orange, with shade warmer than light. Puddles are flat grey sheets. |
| Final noon | 0.36 | 5.4 | 0.72 | 1.51 | The noon frame washed mustard-olive. Sick, but as a filter: the light itself is unchanged. |
| Final hours | 0.08 | 4.8 | 0.54 | 2.41 | Red-orange storm sky top left, ordinary blue night on the ground. Nothing on the ground knows about the sky or the Moon. |

### North, East and West Clock Town

| Frame | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| North, noon | 0.44 | 3.4 | 0.75 | 1.44 | Mustard walls, mustard path: the most saturated daytime frame. (View is from the gate passage, behind the guard.) |
| North, night | 0.14 | 3.9 | 0.34 | 1.81 | Flat blue-grey; the guard glows pale against it. No lamp. |
| North, final hours | 0.09 | 3.6 | 0.42 | 1.37 | Dark purple-brown, no red, no sign of the Moon's light. |
| East, noon | 0.41 | 5.7 | 0.56 | 0.82 | Good sky. Every wall face equally bright; shade warmer than light. |
| East, night | 0.18 | 6.2 | 0.40 | 2.22 | Fine starfield over a town that is the noon frame in violet: no pools, no windows, too bright. |
| West, noon | 0.58 | 4.7 | 0.63 | 0.97 | A hard sun shadow on the sand shows what contrast could do; the rest is yellow. (Half the view is a wall.) |
| West, night | 0.17 | 4.1 | 0.31 | 0.99 | Grey-teal and flat, no warm/cool separation at all. |

### Termina Field

| State | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Dawn | 0.24 | 5.8 | 0.57 | 1.13 | Salmon sky, dark brown ground, mustard path. No mist, no sense of where the sun is. A purple slope on the right that belongs to no light in the frame. |
| Noon | 0.36 | 6.4 | 0.51 | 1.35 | The most natural daytime frame: blue sky, cumulus, grass. But the far tree line has no haze, the path is yellow, and the lighting is flat. |
| Dusk | 0.16 | 6.4 | 0.75 | 1.24 | Strong orange sky, black tree line, ground too dark to show long shadows or rim light. Reads as sunset; nothing is backlit. |
| Night | 0.12 | 4.9 | 0.47 | 2.86 | Stars and a dark blue field, spoiled by large flat firefly discs; the grass near Link is too green for night. |
| Rain | 0.25 | 6.3 | 0.56 | 0.53 | No rain at this spot. Ground dimmed and brown under a bright cumulus sky. |
| Final noon | 0.32 | 7.0 | 0.80 | 0.83 | Olive-mustard wash over the noon frame. |
| Final hours | 0.08 | 5.3 | 0.49 | 2.03 | Red sky, blue ground, pink path, fireflies. |

### Southern Swamp, poisoned

| State | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Dawn | 0.21 | 3.4 | 0.36 | 0.83 | Dim grey-olive. No mist over the water, no shafts, no purple. |
| Noon | 0.35 | 2.9 | 0.39 | 1.20 | Flat grey-green: an overcast afternoon in an ordinary wood. The sickness is absent. |
| Dusk | 0.23 | 3.4 | 0.57 | 1.35 | The same frame in orange-brown. |
| Night | 0.17 | 4.0 | 0.39 | 1.77 | Blue-green with firefly discs. |

### Southern Swamp, cleared

| State | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Dawn | 0.27 | 2.8 | 0.57 | 0.96 | Yellow-brown grass, orange sky, flat. |
| Noon | 0.42 | 2.8 | 0.63 | 1.45 | Grass pushed to acid yellow-green in the foreground. The same place as the poisoned swamp with the saturation turned up, not a place that has recovered. |
| Dusk | 0.28 | 2.8 | 0.79 | 1.42 | Orange grass. |
| Night | 0.20 | 3.0 | 0.46 | 2.00 | Green grass at night, fireflies. |

### Mountain Village (winter) and Snowhead

| Frame | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Village, dawn | 0.57 | 4.0 | 0.26 | 1.73 | Lilac and pleasant, but as bright as midday elsewhere. |
| Village, noon | 0.78 | 3.1 | 0.24 | 1.38 | Tree shadows on snow, blue shade: the right idea. Nearly clipping (highlights 0.97), and the air is clear. |
| Village, dusk | 0.41 | 2.7 | 0.29 | 1.79 | Snow turned brown-mauve: dull sepia rather than a cold evening. |
| Village, night | 0.27 | 3.9 | 0.70 | 1.66 | Electric cobalt. The snow mounds glow. |
| Snowhead, dawn | 0.53 | 2.7 | 0.30 | 1.44 | Lilac; clean; no weather. |
| Snowhead, noon | 0.57 | 3.8 | 0.35 | 1.89 | White path between blue-violet ice walls, far peaks sharp. A clear winter day where the game wants a blizzard. |
| Snowhead, dusk | 0.43 | 2.0 | 0.28 | 1.46 | Brown-mauve, the flattest frame in the set (2 stops). |
| Snowhead, night | 0.26 | 3.6 | 0.61 | 1.23 | Electric blue again. |

### Great Bay Coast

| Frame | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Sea, dawn | 0.26 | 4.9 | 0.47 | 1.84 | Dramatic orange sky over dead maroon sand; the glow on the horizon is in the west. |
| Sea, noon | 0.68 | 3.8 | 0.28 | 1.79 | The horizon band and the palm's shadow are good. Heavy cloud, dull dark water without a single glint, flat beige sand: pleasant and ordinary, not sparkling. |
| Sea, dusk | 0.28 | 4.0 | 0.74 | 1.27 | Sun disc and a glitter path: the best frame of the audit. Still one hue; sand and palm are orange, and there is no cool shade to set it off. |
| Sea, night | 0.17 | 4.2 | 0.69 | 2.46 | Ultramarine. The sea is brighter than the beach and looks self-lit. |
| Beach, dawn / noon / dusk / night | 0.18 / 0.47 / 0.25 / 0.12 | 3.6-6.1 | 0.29-0.79 | 1.2-1.5 | The cliff-side view: maroon, beige, rust, blue. Four tints of one flat slope. |

### Ikana Canyon and Stone Tower

| Frame | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Canyon, dawn | 0.41 | 3.1 | 0.33 | 1.25 | Beige stone, no key, no shadow. |
| Canyon, noon | 0.60 | 3.0 | 0.24 | 1.11 | The same frame, brighter. Grey, not bleached; calm, not haunted. |
| Canyon, dusk | 0.44 | 3.1 | 0.41 | 1.22 | The same frame, slightly warmer. All three daytime states span 3.0 to 3.1 stops. |
| Canyon, night | 0.23 | 4.0 | 0.19 | 1.14 | The spirit flame and its pool are right. The rest is an even grey that is too bright to be night. |
| Tower, dawn / noon / dusk | 0.06 / 0.12 / 0.06 | 9.5-10.1 | 0.70-0.78 | 0.5-1.1 | A saturated orange floor under walls that are pure black (11-16% of the frame). The floor is lit from above and nothing fills the walls. Looks broken rather than monumental. |
| Tower, night | 0.05 | 7.5 | 0.48 | 0.57 | The same, darker; 21% pure black. |

### Enclosed places

| Frame | Median | Range | Sat | Split | Critique |
| --- | ---: | ---: | ---: | ---: | --- |
| Stock Pot Inn, 12:00 and 23:00 | 0.12 | 4.9 | 0.57 | 0.46 | The same bytes at both hours. Dim, greenish, flat; a lit doorway that lights nothing. |
| Woodfall Temple | 0.07 | 5.1 | 0.34 | 0.68 | Green-black murk with a pale fogged far wall. Hard to read, and not because of deliberate shadow. |
| Snowhead Temple | 0.39 | 4.9 | 0.49 | 1.55 | Clean and bright, in saturated blue-violet. Ice as a paint colour: no glow from within, no warm contrast. |
| Stone Tower Temple | 0.15 | 5.6 | 0.71 | 0.77 | Orange brick, orange floor, orange light. |
| Great Bay Temple | 0.13 | 6.1 | 0.40 | 1.55 | Olive walls with glowing lamp discs. The warm/cool idea is there; the lamps do not light. |
| Zora Hall | 0.22 | 4.3 | 0.75 | 0.92 | Ultramarine water showing its tiling pattern, teal shore, no caustics on any wall in view. Garish and flat. |
| Grotto | 0.06 | 6.8 | 0.57 | 0.36 | The native beam polygon and a white blob for its pool. The rest is near black without shape. |

## Things seen that belong to other owners

- **Link is pink in the grotto** (tunic, hat and shield; `grotto_day.png`). Looks like the character light taking the
  grotto's light colour. For the character agent.
- **Termina Field, beside the paved path:** the recessed panels render black with a spotted rim at every hour
  (`termina_field_noon.png`). For the texture agent.
- **Snowhead's ice walls** show stretched texture; **Zora Hall's water** shows its tile pattern. For the texture and
  water owners.
- **Night fireflies** are too large and flat (Field, both swamps).

## Limits of this audit

- One viewpoint per place, and several are poor (North and West Clock Town are half wall; the swamp view has no water
  in it).
- Not covered: Woods of Mystery, Deku Palace, Woodfall exterior, Goron Village, Zora Cape, Pirates' Fortress, Ikana
  Graveyard and Castle, the inverted tower, Romani Ranch, the Moon, any cutscene, underwater.
- Frozen frames show nothing of motion: wind, cloud drift, flicker, adaptation.
- The lit:shade figures are percentile measurements inside hand-placed boxes on one material, not calibrated patches.
- Rain was captured only where the game rains at the capture spot (South Clock Town).
