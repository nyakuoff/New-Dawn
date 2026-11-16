# Texture classification and mapping data

Data for the ND-016 texture pipeline: which world textures a CC0 practical-set material can replace, which are unique painted art, how big
each is in the world, which texture plays which role in two-texture draws, and which unique textures have a *Majora's Mask 3D* counterpart.
Tables (names and numbers only): `mm/assets/custom/newdawn_textures/` (schema in its README). Tools and method: `tools/newdawn/texclassify/`.
Review sheets (game-derived, local): `~/dev/ghm-mods/showcase/texture-classify/`.

## Result (2026-11-16)

**7,527 textures** (3,161 scene, 4,274 object, 92 shared `misc/scene_texture_*`; UI, fonts and TLUT palettes excluded), 4,232 of them
visually reviewed (`rv: 1`; the head covers 99.5 % of scene and 99 % of object visible surface, plus a second pass over the 324 textures that
dominate the measured screens).

| class | n | class | n |
|---|---:|---|---:|
| unique | 3,413 | rock | 179 |
| skip | 2,654 | dirt | 154 |
| foliage | 153 | flagstone | 148 |
| wood_planks | 124 | grass | 119 |
| water | 113 | moss | 90 |
| ice_snow | 85 | wood_beam | 72 |
| sand | 68 | thatch | 35 |
| metal | 33 | brick | 32 |
| cobble | 25 | plaster | 18 |
| mud | 6 | fabric | 4 |
| rope | 2 | roof_tiles | 0 |

Of 7,527: **1,460 replaceable** (material class, `conf >= 0.6`), 3,413 unique, 2,654 skip. By group: scenes 1,050 / 1,484 / 627
(replaceable / unique / skip), objects 365 / 1,901 / 2,008 (characters, props, effects: mostly unique or skip), misc 45 / 28 / 19.

N64 world textures are mostly painted, not photo-like, so **brick, cobble, plaster, fabric, rope and roof_tiles are rare** (roof tiles and
shingles do not occur as tileable textures). The bulk of the replaceable set is ground (rock, dirt, sand, grass, moss), flagstone, planks,
foliage cut-outs, water and snow.

### Replaceable vs unique per scene

Measured on screen (texture-ID buffer, six views per scene, 1080p; world + object pixels; unique includes low-confidence material labels):

| scene | surface area R / U / S | **on-screen pixels R / U / S** |
|---|---|---|
| South Clock Town | 59% / 40% / 1% | **32% / 65% / 3%** |
| East Clock Town | 50% / 49% / 1% | **55% / 35% / 10%** |
| Termina Field | 54% / 8% / 37% | **45% / 47% / 9%** |
| Southern Swamp (poison) | 71% / 4% / 25% | **97% / 2% / 2%** |
| Great Bay Coast | 99% / 1% / 0% | **87% / 12% / 1%** |
| Snowhead Temple | 84% / 16% / 1% | **86% / 11% / 3%** |
| Ikana Canyon | 92% / 8% / 0% | **88% / 11% / 1%** |
| Stone Tower Temple | 27% / 71% / 2% | **76% / 21% / 3%** |
| Woodfall Temple | 84% / 15% / 1% | **88% / 3% / 9%** |
| Stock Pot Inn | 61% / 36% / 3% | **39% / 53% / 7%** |

Pooled over those ten: **69 % replaceable, 26 % unique, 5 % skip on screen**. Over all 100 scenes the static-area estimate is 69 % mean (76 %
median) replaceable, 20 % unique, 10 % skip (opaque and cut-out surface). Static area is **not** visibility (a huge back wall counts as much as
a floor you stand on), which is why the two disagree in Clock Town, Stone Tower and Termina Field: use the screen column where it exists and
treat the area column as a scene-content indicator. The clock-town hub scenes are the worst for the replace-first plan (40 to 65 % unique:
painted pillars, friezes, banners, signs); the temples and the open fields are 75 to 95 % replaceable. `scene_stats.json` has all 100 scenes.

### 3DS counterparts for unique art

Of the 1,484 unique (or low-confidence) **scene** textures:

| | candidate (weak or better) | **confident** (`share >= 0.6`, `coverage >= 0.3`, `margin >= 0.2`) |
|---|---:|---:|
| by texture count | 78 % | **59 %** (874) |
| by static surface area | 64 % | **32 %** |
| by measured on-screen share | 70 % | **24 %** |

A confident counterpart exists for most unique textures but for only about a quarter of the *pixels* of unique art, because the large unique
surfaces (big painted walls) are covered in the 3DS version by several atlas textures, so no single 3DS texture wins the vote. The 3DS
textures are redrawn atlases; the table gives the texture name per match, not a UV rectangle. Pixel template matching found nothing reliable
(see the method README). Actors and characters are not matched.

## Validation

- **Coverage of the walk**: all 683 textures seen by the texture spike's ID-buffer captures are found by the static interpreter; 6,748 of the
  8,379 inventoried entries (incl. palettes) are drawn through display lists (the rest are eyes/mouths and palettes selected by actor code).
- **Against the spike's by-eye labels** (33 textures): 30 agree after the class-compatibility mapping (stone ~ flagstone/cobble/rock/brick, ...); the 3 that differ
  are kept `unique` on purpose (a carved frieze with a window, two bolted 16x64 wooden strips); three other differences were corrected after
  looking at them again (`late_corrections`).
- **Spot checks**: the 333 tail material labels were all inspected; ~12 % needed correction. Reviewed head and pass-2 sheets are in the showcase folder.

## Caveats and unknowns

- Class boundaries inside the earthy family (dirt / sand / mud / rock / grass / moss) were set by eye on 32 to 128 px textures and are fuzzy;
  treat them as "ground". Greyscale (I4/IA) textures are classified as the material they imitate but are coloured by combiner colours at run time.
- `rv: 0` entries (3,295, almost all in the tail, mostly `unique`/`skip`) were not individually seen: some are replaceable materials that
  stayed `unique`. That error direction is the safe one, but it undercounts the replaceable share (the tail holds <1 % of visible surface).
- Tree billboards and fence cut-outs are `unique`; only leaf and bush cut-outs are `foliage`.
- `scale.json` units: `metres_per_unit = 0.025` is an assumption (Link about 1.5 m for about 60 units). Object-space scales ignore the actor's
  draw scale. Textures drawn only via actor code (eyes, mouths) have no entry.
- Layer roles for `modulate` pairs are heuristic (saturation vs edge energy); `lerp_prim` pairs (294 of 809) are exact alternates.
- Area stats count a draw once by its base texture; translucent draws (water surfaces, glass, fog planes) are excluded and reported separately.
- The measured screen shares come from six yaw angles at each scene's spawn point (the texture spike's captures), not a walked route.
