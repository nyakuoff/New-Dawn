# New Dawn look bible

**Status:** approved by the maintainer on 2026-11-16, with the dark night of section 4 and the choices recorded in
section 9. It is the target, not a description of the game: what is built so far is in `docs/LOOK_PLAN.md`. The audit
that led to it is `docs/LOOK_AUDIT.md`.

This document defines what every place in the game should look like at every time of day. It is the target the look
profiles are authored against, and its vocabulary (section 3) is the profile schema. The anchor is ND-014: a
hyper-detailed, tactile world that looks like a practical-effects film set, dark and beautiful, with colourful doll-like
characters in it. Not cartoony. Matching the original's lighting is not a goal; keeping each place recognisable is.

## 1. The premise: a film set, not a filter

Today each place gets its identity from a colour grade laid over evenly lit geometry. A film set gets its identity from
where the light comes from, what the air does to it, and what is left dark. The whole bible follows from one decision:

> **Identity comes from light, air and sky. The grade is the last tenth.**

So a region is defined, in this order, by:
1. its **key** (one dominant source with a direction, a colour and a hardness),
2. its **fill** (what the shade is made of: sky, bounce, water glow),
3. its **air** (haze, mist, dust, snow: what separates near from far and catches the key),
4. its **practicals** (lamps, fires, windows, glowing things that light their surroundings),
5. and only then its **tone and colour trim**.

## 2. Ten rules of the image

1. **One key.** Every frame has one readable dominant light. Outdoors it is the sun or the Moon; indoors a window or a
   flame. If you cannot point to where the light comes from, the frame is wrong.
2. **Shade is a colour, not a darker version of the light.** Sunlit surfaces are warm, shade takes the sky's colour.
   Light and shade always differ in hue as well as in level (section 4 gives the temperatures).
3. **White is white at noon.** A neutral surface in midday sun reads neutral (red/blue between 0.9 and 1.1; the audit
   measured 3.4 on white canvas in Clock Town and 0.78 on snow). Warmth is something dawn, dusk and lamplight add; it
   is not a constant cast over the game.
4. **Dark is allowed.** Shadows and nights go deep and stay clean (true black is permitted, tinted grey is not), and the
   eye is led by pools of light. Readability comes from rim light and silhouettes against lit air, not from lifting the
   whole frame.
5. **Air is always there.** Every outdoor frame has three depth planes: a crisp, contrasty foreground, a midground
   lifted and tinted by haze, and a background that dissolves towards the horizon colour. Haze is brighter and warmer
   towards the sun and cooler away from it; it is never one flat fog colour.
6. **Every visible source lights something.** A torch, a lantern, a window, lava, poison, a fairy: each throws its own
   coloured light and its own shadows. No bright thing without an effect, and no brightness without a source.
7. **Highlights roll off like film.** Bright things keep detail and ease towards white over several stops. Bloom is
   reserved for real emitters, glints and sun on snow, sand and water; it never softens the whole frame (the two
   "dream" places in section 8 are the exception).
8. **Colour is rationed.** Each region has one dominant hue, one opposing hue in the shade or the accents, and one
   small saturated accent. Saturation lives in the midtones; shadows lose a little and lean to the fill's hue;
   highlights lose it as they brighten.
9. **Characters are the colourful thing in the frame.** They are separated from the set by a rim or kicker, keep their
   own palette under any grade, and never clip. The set is tactile and restrained so that the dolls stand out.
10. **The clock is readable in the light.** At a glance the player can tell morning from noon from evening from night,
    and day one from the final day.

## 3. Vocabulary and how it is measured

These are the dials a profile sets. Outcome targets are measured on the frozen audit frames
(`tools/newdawn/lookdev_audit.sh`) with `tools/newdawn/lookdev_stats.py`, HUD cropped.

| Dial | Meaning | How it is checked |
| --- | --- | --- |
| Key | Dominant light: colour temperature, strength relative to the clear noon sun, hardness | Colour of a neutral sunlit patch |
| Fill | Sky and bounce light in the shade: colour temperature, strength | Colour of the same material in cast shadow |
| Lit:shade | Linear luminance of one material in the key over the same material in cast shadow | Patch pair, sRGB decoded |
| Shadow density | How much of the key a cast shadow removes (100% = only fill remains) | Same patch pair |
| Frame median, highlights | Display luma at the 50th and 95th percentile | `p50`, `p95` |
| Range | Stops between the 5th and 95th percentile in linear light | `range` |
| Split | Red/blue of the bright quarter over red/blue of the dark quarter; above 1 means warm light, cool shade | `split` |
| Visibility | Distance at which haze has removed 95% of contrast, at eye level | Authoring value, world units |
| Haze height | Height over which haze thins to about a third | Authoring value, world units |
| Shafts | Strength of visible light beams in the air | By eye, on the reference view |
| Bloom | Threshold (in multiples of diffuse white) and strength | By eye, 1:1 crops |

Distances and heights below are starting values for the look editor, not measurements.

## 4. The base ladder: one day of light

Every clock-lit place starts from this ladder and then departs from it as its section says. "Key" is relative to the
clear noon sun. Temperatures are the rendered colour, not physical values.

| State (clock) | Key | Fill | Lit:shade | Cast shadows | Frame median / highlights | Air |
| --- | --- | --- | --- | --- | --- | --- |
| Blue hour (04:30-05:45) | None; a pale glow low in the east | Deep blue, 12000 K, very soft | 1:1 | None, contact shading only | 0.12 / 0.40 | Ground mist; visibility down 40% |
| Sunrise (06:00-07:30) | Pale gold, 3600 K rising to 4800 K, 45% | Sky, 9000 K | 6:1 | Very long, dense (85%), soft-edged | 0.28 / 0.80 | Mist and backlit haze; the strongest shafts of the day |
| Day (09:00-15:30) | 5600 K, 100% | Sky 7500 K plus ground bounce | 5:1 | Crisp, 80% | 0.40 / 0.88 | Clear; haze only from the midground out |
| Golden hour (16:00-17:30) | 3400 K, 70% | 9000 K | 6:1 | Long | 0.36 / 0.86 | Warm haze towards the sun, rim light on everything facing away |
| Sunset (17:30-18:30) | 2200 K, 30%, grazing | 11000 K with a magenta afterglow | 4:1 | Longest, fading out | 0.26 / 0.78 | The widest warm/cool separation of the day |
| Blue hour (18:30-19:30) | None; lamps become the keys | 12000 K | 1:1 away from lamps | Lamp shadows only | 0.15 / 0.60 | Lamps and windows are on before the sky is dark |
| Night (19:30-04:30) | The Moon: silver-blue, 3% | Night sky, 0.8% | 4:1 | Soft, 75% | 0.09 / 0.35; lamp pools up to 0.85 | Thin; mist in hollows and over water |
| Overcast or rain | Sun diffused to 20% | Sky dominant, 6800 K | 1.5:1 | Contact only | 0.33 / 0.68 | Visibility halved, grey-blue |

**Dawn and dusk are opposites.** Today they are the same orange frame. Dawn is cool, pale and misty: the light is
arriving, the shade is still night-blue, the colours are washed clean. Dusk is hot, saturated and dusty: the light is
leaving, the shade goes violet, lamps are already lit.

**Tone curve (one for the whole game).** Scene-referred and filmic: mid-grey lands at 0.42 on the display, a white
card in noon key and fill at 0.86, and light up to six times that white still has gradation before it reaches 0.98.
The toe keeps shape in the darks without lifting black. Highlights lose saturation towards the colour of the light and
then white. Today's curve is the identity up to 0.76 and then a short shoulder, which is why sunlit surfaces look
painted and nothing glows.

**Exposure.** Each state has an authored exposure. On top of it the camera adapts by at most one stop either way, over
about a second: stepping out of the Inn at noon dazzles briefly, a cave mouth is a black hole until you are inside it.

## 5. The three-day arc

The original tells the three days mostly through the sky and the music. The remake tells them through the light. A
single value, **dread** (0 to 1), rises over the cycle and bends every outdoor profile.

| When | Dread | What changes everywhere outdoors |
| --- | --- | --- |
| Day 1 | 0 | The ladder as written: clear air, fair-weather cumulus, the cleanest whites of the cycle. |
| Night 1 | 0.1 | Clear, starry, the Moon small and cold. |
| Day 2 | 0.3 | Overcast, rain from morning to late afternoon where the game rains: slate light, wet ground, lamps lit by day. Dry regions get a heavy, lower sky. |
| Night 2 | 0.4 | The cloud breaks; a harder, colder moonlight; the Moon visibly larger and its glow warmer than the sky. |
| Day 3 | 0.5 rising to 0.7 | The sun weakens by up to a stop and reddens through a sulphur-yellow storm deck. Fill loses its blue and turns olive-brown, so shade no longer feels cool or safe. Greens lose a third of their saturation. Contrast rises. Fine ash drifts on the wind. |
| Final night (18:00-00:00) | 0.7 rising to 0.85 | The Moon becomes a light source: an ember-red key from its direction, with its own downward shadows, over a sky lit from below by its glow. Lamps look small and pale against it. |
| The last six hours | 0.85 rising to 1 | Eclipse light: the Moon covers much of the sky, ambient falls a stop, everything has a red rim from above and black beneath. The sky-facing side of every surface is red; only lamps, fireworks and windows are another colour. |
| Dawn of the first day (after the Song of Time) | 0 | The clearest, coolest morning in the game, deliberately: relief by contrast. |

Ikana gets half the bend (it is already dead); interiors get it only through their windows.

## 6. Regions

Each region lists its identity, its palette, and how it departs from the ladder. Hex values are swatches for the look
editor, chosen for this document.

### 6.1 Clock Town (south, north, east, west, Laundry Pool, rooftop)

**Identity.** A festival town on a studio backlot: sun-baked plaster and timber, deep awning shade, lantern light.
**Palette.** Light: warm ivory `#F2DDB0`. Shade: slate blue-violet `#56607F`. Accents: festival red `#B5362A`, lantern
amber `#FF9A3C`. **Lighting language:** a hill town at siesta; a golden-hour backlot street; a lantern-lit night
market.

| State | Target |
| --- | --- |
| Sunrise | The sun clears the east wall first: upper storeys and the Clock Tower face are gold while the streets are still in blue shade (8:1 between tower and street). Lanterns burn until 06:30. A thin mist lies in the plazas. |
| Day | Neutral whites, 5:1. Awnings and arcades throw hard, deep shade, and sunlit paving bounces ochre light up into it. Dust motes in the beams between buildings. Cobalt sky, small cumulus. Frame median 0.40. |
| Golden hour, sunset | An amber key rakes along the streets; shade goes violet. Lanterns and windows come on at 17:00, so for an hour the town is lit by both. The clock face catches the last sun. |
| Night | Cobalt moonlight from above and behind the tower, stars over the roofs. Lantern pools at 2300 K, 10:1 against the moonlit paving. Windows glow. Alleys are nearly black with one practical each. Laundry Pool: Moon glitter on the water, fireflies. |
| Rain (day 2) | Slate overcast, visibility about 3500, wet cobbles mirroring the lanterns, which stay lit all day. The reds and the banners are the only saturated things in the frame. |
| Final day | Section 5, at full strength: this is where the Moon hangs. On the last night the fireworks flash the plaza in their own colours against the red sky. |

### 6.2 Termina Field (and the roads, Milk Road, Romani Ranch)

**Identity.** The open air between four worlds. **Palette.** Sunlit grass `#86A63C`, grass in shade `#2F5B4A`, sky
`#3F7FD9`, haze warm white `#F3E9D2`. **Lighting language:** big-sky landscape photography; the long backlit grass of a
pastoral epic.

- **The signature state is golden hour:** a low sun, every tuft, tree and stone rim-lit, shadows three times as long as
  what casts them, and backlit haze filling the hollows. This is the first thing the player sees on leaving town at the
  end of day one and it should be the image people remember.
- **Cloud shadows** drift across the plain at noon, so the field is never one flat green.
- **Four horizons.** The haze takes the colour of the region it looks towards: cold blue-white with a snow plume off the
  peaks to the north, bright pearl to the west over the sea, humid grey-green with low mist over the tree line to the
  south, dusty ochre fading to violet to the east. The town walls sit in the middle as the warm centre.
- **Night:** the biggest sky in the game. Milky Way, silver grass with a Moon rim, fireflies in summer air; ground mist
  gathers before dawn and burns off in shafts through the tree lines at sunrise.
- **Romani Ranch:** the ladder at its warmest and cleanest by day; on the first night the invasion's lights are the
  only cold colour in the frame.

### 6.3 Southern Swamp, poisoned (and Woodfall before its temple is cleared)

**Identity.** A drowned, sick forest under a bruise-coloured sky. **Palette.** Sunlit mist chartreuse `#A8B84A`, shade
aubergine `#3A2A4F`, water toxic violet `#7A3FA0`. **Lighting language:** the smoke-filled forest sets of 1980s
creature-shop fantasy films; a bayou at dawn.

| State | Target |
| --- | --- |
| Day | Low key: frame median 0.22, a stop under the ladder. The sun is never clean; it is a pale disc in the deck and reaches the ground only as shafts through the canopy. Inside a shaft 10:1; outside it there is only fill. The water lights the undersides of everything violet. |
| Air | The thickest in the game: visibility about 2200, a mist layer hugging the water (haze height 150) that drifts, and a thinner haze up to the canopy. Shafts are the main event here, strongest of any region. |
| Sunset | The deck goes rust and magenta, the mist turns the colour of old brass. |
| Night | Almost black. The poison glows faintly on its own, spores drift like slow sparks, and the Tourist Centre's torches are a warm island. |

**Cleared:** the morning after a fever. Emerald water with caustics on the roots, golden shafts through a wet canopy,
white mist that thins by noon, blue sky in the gaps, flowers. Frame median back on the ladder, saturation up.

### 6.4 Woodfall, Deku Palace, Woods of Mystery, Woodfall Temple

- **Woods of Mystery:** a cathedral forest. Nine tenths shade, a few hard shafts, green bounce, mist between the trunks
  (visibility 1800).
- **Deku Palace:** a royal garden. Dappled light through leaves moving in the wind, pollen in the beams, warm stone.
- **Woodfall Temple:** torchlight and light wells. Frame median 0.14. Torches at 12:1 with real shadows; daylight falls
  through the roof openings as shafts in humid air; moss glints; the poison glows up from below until it is cleansed,
  then the water is clear and throws caustics on the walls.

### 6.5 Snowhead in winter (Mountain Village, Path to Snowhead, Snowhead, Goron Village, Snowhead Temple)

**Identity.** A world erased by weather and lit from inside the cloud. **Palette.** Snow in light `#EEF4FF`, snow in
shade `#7FA6E0` deepening to `#4C6FB8`, firelight `#FF8A3A`. **Lighting language:** polar expedition photography in a
white-out; blue-hour snowfields; ice lit from behind.

| State | Target |
| --- | --- |
| Day | High key and nearly shadowless: lit:shade 1.5:1, frame median 0.50, highlights 0.92 with texture still in the snow. Contrast comes from dark rock, timber and figures against white, not from the light. Shade on snow is plainly blue. |
| Blizzard (the paths and Snowhead itself) | Visibility 700-1500, pulsing with the gusts. Wind-driven streaks, a glare spot where the sun is, wide soft bloom. Figures appear out of the white. |
| Sunset | The deck thins at the horizon: a band of pale gold under grey, long blue shadows for a few minutes, rose on the peaks. |
| Night | The brightest night in the game (frame median 0.13): snow under a hidden Moon, cobalt and cyan. The smithy's windows and the Goron fires are the only warm things for a mile. |
| Spring (after the temple) | Alpine clarity: the longest visibility in the game, saturated greens against a deep sky, meltwater glitter. |
| Snowhead Temple | Ice lit from within: cyan glow through the walls, cold fill, and fire rooms in full orange against it. Frost haze low to the floor. |

### 6.6 Great Bay (Coast, Cape, Zora Hall, Pirates' Fortress, Pinnacle Rock, Great Bay Temple)

**Identity.** Light itself: sun, salt and water. **Palette.** Shallows turquoise `#2FB7B0`, deep water `#1E4FA8`, sand
`#F0DDB0`, dusk coral `#FF8FA0`. **Lighting language:** a Mediterranean noon; sea-cave caustics; a rock-concert stage.

| State | Target |
| --- | --- |
| Day | The highest key outdoors: frame median 0.46. A glitter path on the sea towards the sun, with bloom. Sand bounces warm light into shade, so shade here is the brightest in the game (4:1). A low, bright marine layer whitens the horizon (visibility 9000, haze height 250). |
| Murky bay (before the temple) | The same place with the light taken out: grey-green opaque water, thicker haze (visibility 5000), the sun half a stop down, no glitter. |
| Sunset | The hero sunset of the game: the sun goes down into the sea, a long glitter path, coral and magenta afterglow, palms and the laboratory in silhouette (8:1). |
| Night | A Moon path on the water, the laboratory's window, pale surf. |
| Zora Hall | A grotto under the sea: caustics from the pool on walls and ceiling, teal fill, frame median 0.20. The stage is lit as a stage: warm coloured spots and a rim on the performers. |
| Pirates' Fortress | Hot stone and hard courtyard shade by day; at night, torch pools and deep shadow, with the guards' lanterns clearly the brightest moving things. |
| Great Bay Temple | Machinery in the dark: cyan light from the water below, amber lamps above, steam, wet metal. |

### 6.7 Ikana (Road, Canyon, Graveyard, Castle, the Well, Stone Tower and its temple)

**Identity.** A kingdom bleached by a dead sun; at night, a cold Moon and things that glow. **Palette.** Bone `#D9CFB8`,
dust ochre `#B89A6A`, shade dusty violet `#5A4E78`, spirit green `#6FE8C8`, rust `#8A3A22`. **Lighting language:** a
bleach-bypass western; moonlit ruins; dust-storm light.

| State | Target |
| --- | --- |
| Day, cursed | Hard top light, short dense shadows (90%), highlights at 0.93, saturation at about 0.6 of the ladder. The game switches its sun off here, so today Ikana has no key and no shadows at all; the profile supplies the key. A pale violet-grey sky with no blue. Ochre dust haze (visibility 6000, haze height 600). Heat shimmer over the ground beyond the middle distance while the sun is high. |
| Sunset | The only warm hour: rust light on the cliff tops while the canyon floor is already violet. |
| Night | The coldest look in the game. A hard, crisp Moon key (90% shadows), silver-cyan. Fog pools in the river gorge and between the graves (haze height 80). The only lights are spirit green and blue, with one exception: the Music Box House window, the single warm light in Ikana. |
| Curse lifted | The same bones in clean air: blue sky, gold sandstone, saturation 0.9, a sparkling river. |
| Stone Tower | Monumental and vertical: rust blocks, strong shafts down the shaft of the tower. Inverted, the sky is underneath: fill comes from below, violet-indigo, and dust rises instead of falling. |
| Castle, Well | Dry torchlight and hard sun shafts through broken roofs; the beams the puzzles use are visible in the dust. |

### 6.8 Interiors (the Inn, shops, houses, the Milk Bar, offices)

**Identity.** Rooms lit by what is in them. **Lighting language:** motivated practical lighting; candlelit period
interiors.

- Frame median 0.20; corners fall to 0.04; lamp pools reach 0.80. Each room has one key practical and one accent.
- Lamps and candles 2200-2700 K, hearths 2000 K. Windows follow the clock: cool daylight shafts with dust by day,
  orange at sunset, dim blue at night, red on the final night.
- Polished wood, glass and brass catch the lamps. Nothing is lit that a lamp or a window does not reach, except by
  bounce.
- The Stock Pot Inn at night is the reference: amber, close, dark in the corners, the kitchen fire the brightest thing.
- The Milk Bar adds smoke in the air and a stage light.

### 6.9 Caves, grottos, fairy fountains

- **Grottos:** black, with one shaft of daylight falling from the hole above, damp rock catching it, and a cool bounce
  off the floor. Anything that glows (mushrooms, water, a Gossip Stone's eye) is the accent.
- **Fairy fountains** are one of the two dream places: iridescent pink and aqua caustics, drifting sparkles, deliberate
  soft bloom, no hard shadow.

### 6.10 The Moon

The other dream place. The meadow inside the Moon is impossibly serene: high key, half a stop over, pastel, soft
shadows, a clean horizon, one tree. It should feel too perfect after the final night. The fights that follow drop to
black with saturated coloured light.

## 7. Characters in the look

The character light path (`docs/CHARACTER_LIGHTING.md`) keeps its own shading. The profile feeds it three things: the
key, the fill, and a **rim** (colour and strength) chosen per state so that figures separate from the set: the sky by
day, the low sun at golden hour, the Moon at night, the nearest practical indoors, the red Moon on the final night.
Characters sit a third of a stop above the set around them and never clip.

## 8. What must not change

- **Gameplay visibility.** Whatever the original shows clearly stays readable: haze must leave at least 70% contrast at
  the distance where the original's fog begins, and must be opaque by the distance where the original's fog is, so
  nothing pops in. A blizzard or the swamp mist may be thicker to look at, never thicker to play in.
- **Dark rooms, stealth and puzzles.** Enemies, guards, switches, light beams and Lens of Truth surfaces stay as
  visible as they are today. Night and interiors get darker around them, not over them.
- **Scripted light.** Cutscenes, boss introductions, lightning and story changes still drive the look; the profile
  follows the script (see the plan).
- **Recognition.** The poison is purple, Ikana's sky is dead, the mountain is snowed in, the final day's sky is sick.
  Those are kept and pushed further, not replaced.
- **No gameplay state is written.** Everything above reads the scene, the clock, the day and the story flags.

## 9. Beyond the brief

Each is render-only. The maintainer's decision of 2026-11-16 is in the last column.

| Idea | Where | Why | Decision |
| --- | --- | --- | --- |
| Cloud shadows moving over the ground | Field, coast, canyon | Breaks up flat noon light for almost no cost | In |
| Horizon haze coloured by direction | Termina Field | Makes the hub feel connected to four different worlds | In |
| The Moon as a red shadow-casting key | Final night, everywhere outdoors | The strongest single image of dread available | In |
| Lit windows and lanterns from 17:00 | Clock Town, villages | The town visibly comes alive at dusk | In |
| Heat shimmer | Ikana by day | Sells the dead heat | In |
| Aurora | Snowhead nights | Not in the original; a bold addition | In |
| Phosphorescent surf | Great Bay nights | Small, memorable | In |
| Overcast day 2 where the game does not rain; ash on day 3 | Outdoors | The three-day arc of section 5 | In (visual only) |
| Clock-driven windows indoors | All interiors with windows | Ties interiors to the three-day clock | Not yet decided |
| Film response: halation, fine grain, slight vignette | Everywhere | The "analog" half of ND-014 | Out |
