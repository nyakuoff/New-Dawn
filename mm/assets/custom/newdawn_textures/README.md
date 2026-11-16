# newdawn_textures: texture classification and mapping tables

**Names and numbers only.** No pixels, no ROM or RomFS bytes, nothing that can be turned back into a game asset (ND-008, ND-016).
The tables describe the game's world and character textures by *resource path* so that the runtime generator
(`feature/texture-pipeline`) can pick a CC0 material, scale it, give layered draws their roles and know which textures
must be upscaled instead of replaced. The tables are produced offline by `tools/newdawn/texclassify/` from the
maintainer's own dumps; see that README for the method and for how to regenerate them.

All files are UTF-8 JSON, one top-level object, `"version": 1`. Keys are **resource paths exactly as in `mm.o2r`**
(`scenes/nonmq/Z2_CLOCKTOWER/Z2_CLOCKTOWERTex_00E880`, `objects/gameplay_keep/gBushLeaf1Tex`,
`misc/scene_texture_06/scene_texture_06_Tex_001C60`). Palette resources (`...TLUT_xxxxxx`) never appear as texture keys;
they appear only as `pal` values.

> **Status of this file.** The schema is fixed; the data files are filled in by the classification run. A file listed
> under "Files" that does not exist yet is still being generated. Fields marked *(optional)* may be absent.

## Files

| file | content |
|---|---|
| `classification.json` | one entry per world/character texture: material class, confidence, review state |
| `scale.json` | UV density and world size per texture |
| `layers.json` | per two-texture draw: which texture plays which role |
| `mm3d_match.json` | for `unique` textures: the matching *Majora's Mask 3D* texture name, with a confidence |
| `scene_stats.json` | per scene: share of world surface that is replaceable / unique / skip |

## Classes

`classes` (in `classification.json`) lists them in a stable order. A texture has exactly one.

| class | meaning | action |
|---|---|---|
| `brick` | regular brick courses | replace (masonry layout) |
| `cobble` | small irregular round or square stones | replace |
| `flagstone` | large flat slabs, paving, ashlar blocks | replace |
| `wood_planks` | boards, floors, doors, crates (fine parallel gaps) | replace |
| `wood_beam` | beams, logs, trunks, bark | replace |
| `plaster` | plaster, stucco, painted walls without a motif | replace |
| `roof_tiles` | shingles, tile roofs | replace |
| `thatch` | straw, reeds, hay | replace |
| `dirt` | soil, packed earth, gravel | replace |
| `sand` | sand, dunes, ripples | replace |
| `mud` | mud, swamp ground, muck | replace |
| `rock` | cliffs, natural rock, rough stone without courses | replace |
| `grass` | grass *ground* texture (not the cut-out tufts) | replace |
| `moss` | moss, overgrown stone, lichen | replace |
| `metal` | metal plates, bars, grates, rivets | replace |
| `fabric` | cloth, canvas, carpets without a motif, wool, leather | replace |
| `rope` | rope, cord, chain-like weave | replace |
| `water` | water surface and water-like fills | replace (water pipeline owns the look) |
| `ice_snow` | snow, ice, frost | replace |
| `foliage` | alpha cut-out leaves, bushes, flowers, vines | replace (cut-out material) |
| `unique` | painted art that must not be replaced: murals, signs, emblems, carvings, faces, text, eyes/mouths, symbols | **upscale** (from MM3D when `mm3d_match.json` has it) |
| `skip` | sky, effects, particles, gradients, lightmaps, fills, shadows, glows: New Dawn already handles these | leave alone |

"Replaceable" below means any class except `unique` and `skip`.

## `classification.json`

```json
{
  "version": 1,
  "classes": ["brick", "cobble", "..."],
  "textures": {
    "scenes/nonmq/Z2_CLOCKTOWER/Z2_CLOCKTOWERTex_00E880": {
      "class": "brick",
      "conf": 0.93,
      "rv": 1,
      "pal": "scenes/nonmq/Z2_CLOCKTOWER/Z2_CLOCKTOWERTLUT_022620",
      "cells": [3, 4]
    }
  }
}
```

- `class`: one of `classes`.
- `conf`: 0 to 1. For `rv: 1` entries it is the confidence after review (corrected entries get 1.0).
  **Replace only when `class` is a material class and `conf >= 0.6`**; below that treat the texture as `unique` (a
  wrongly replaced unique texture is worse than a missed replacement).
- `rv`: `1` if a human-level visual review (contact sheet) covered the entry, `0` if the label is automatic only.
- `pal` *(optional)*: for palette (CI4/CI8) textures, the TLUT resource used by the largest share of the surface that
  draws the texture. Other palettes may exist (`pal_alt`, a list, optional).
- `cells` *(optional)*: `[columns, rows]` of repeating cells inside **one tile** of the texture (bricks per tile
  horizontally and vertically, cobbles, planks, roof-tile courses) from period detection. Only present for the
  structured classes when the detection was clear; absent means "not measured", not "one cell".
- `sub` *(optional)*: free-text sub-type when the class alone is too coarse (`ashlar`, `running_bond`, `vertical`, `paved`).
- `why` *(optional, short)*: the strongest evidence (`clamp`, `eye-name`, `cutout-green`, `override`).

## `scale.json`

```json
{
  "version": 1,
  "metres_per_unit": 0.025,
  "textures": {
    "scenes/nonmq/Z2_CLOCKTOWER/Z2_CLOCKTOWERTex_00E880": {
      "tpu": [0.457, 0.456],
      "tile_units": [70.0, 70.1],
      "tile_m": [1.75, 1.75],
      "space": "scene",
      "area": 5313999,
      "n": 5
    }
  }
}
```

- `tpu`: texels per world unit along the texture's **U and V** (median over the draws that use the texture, weighted by
  surface area). The same value in `scenes/` and `objects/` is not comparable (see `space`).
- `tile_units`: world units covered by **one repeat** of the texture, `[w/tpu_u, h/tpu_v]` (native width and height).
- `tile_m`: `tile_units * metres_per_unit`. `metres_per_unit` is an **assumption** (Link's human form is about 60 units
  tall, taken as about 1.5 m): good to roughly 20 %, not measured. Prefer `tile_units` for relative sizes.
- `space`: `scene` (room meshes, true world units) or `object` (model space of the object; the actor's draw scale is
  applied at run time and is **not** known here, so object `tile_units` are relative only).
- `area`: total surface area (world units squared) over which the sample was taken; `n`: number of distinct draw states.
- Textures that are never drawn through a display list (eyes and mouths selected by actor code) have no entry.

## `layers.json`

Draws that combine two textures (RDP tile 0 and tile 1 both read by the colour combiner).

```json
{
  "version": 1,
  "roles": ["base", "detail", "decal", "lod_dup"],
  "patterns": {
    "modulate":   "TEXEL0 * TEXEL1: both multiply the surface colour",
    "lerp_alpha": "TEXEL0 blended toward TEXEL1 by an alpha (a decal / overlay over a base)",
    "lerp_lod":   "TEXEL0 blended toward TEXEL1 by LOD fraction (mip emulation; usually the same image twice)",
    "other":      "anything else (see `cc`)"
  },
  "pairs": [
    {
      "a": "scenes/nonmq/Z2_00KEIKOKU/Z2_00KEIKOKUTex_01EE50",
      "b": "scenes/nonmq/Z2_00KEIKOKU/Z2_00KEIKOKUTex_02A050",
      "pattern": "modulate",
      "roles": ["base", "detail"],
      "tpu_a": [0.31, 0.31],
      "tpu_b": [0.62, 0.62],
      "scenes": ["scenes/nonmq/Z2_00KEIKOKU"],
      "area": 1234567.0,
      "alpha": "opa",
      "cc": ["(TEXEL1-0)*TEXEL0+0", "(COMBINED-0)*SHADE+0"]
    }
  ]
}
```

- `a` is the texture in RDP **tile 0**, `b` in **tile 1** (both resource paths). `roles` = `[role of a, role of b]`.
- `base` carries the colour and decides the class; `detail` is a tiling overlay (courses, grime, mottling) that is
  multiplied on; `decal` is a unique alpha overlay; `lod_dup` means `a == b` or the pair is only a mip blend: treat the draw as the single texture `a`.
- If one of the two textures is `unique` or `skip` (a lightmap, say), the pair says so through the roles
  (`detail` of a skip texture means "ignore it").
- `tpu_a`, `tpu_b`: texels per world unit as in `scale.json`, **per tile** (tile 1 usually has its own shift).
- `alpha`: `opa`, `cutout` or `xlu` render mode of the draw. `cc`: the colour-combiner cycles for debugging.
- `area`: surface area summed over the scenes that draw the pair.

Draws that read a single texture are not listed (use `classification.json` and `scale.json`).

## `mm3d_match.json`

Only for `unique` textures. **Names and numbers only; nothing from the RomFS is stored.**

```json
{
  "version": 1,
  "textures": {
    "scenes/nonmq/Z2_CLOCKTOWER/Z2_CLOCKTOWERTex_01FD80": {
      "scene": "z2_clocktower",
      "mm3d": "room_00/tex_0012",
      "score": 0.91,
      "margin": 0.18,
      "rot": 0,
      "flip": false
    }
  },
  "coverage": { "...": "summary numbers, see scene_stats.json" }
}
```

- `scene`: the MM3D scene name (lower-case segment name from `tools/newdawn/mm3d/scene-table.json`).
- `mm3d`: an identifier the *local* tool can resolve in the player's own RomFS: `<room or object archive>/<texture index or name>`.
  It means nothing without the player's dump.
- `score`: 0 to 1 perceptual similarity after resizing. `margin`: `score` minus the second-best candidate in the same scene.
  **Use a match only when `score >= 0.75` and `margin >= 0.05`.** Lower-confidence matches are listed with
  `"weak": true` and must not be used without review.
- `rot` (0, 90, 180, 270) and `flip` (the 3DS image is mirrored vertically) describe the transform that made them match.
- A `unique` texture with no entry has no 3DS counterpart that could be found: upscale the N64 original.

## `scene_stats.json`

Per scene folder (`scenes/nonmq/<SCENE>`, `objects/<OBJECT>`):

```json
{
  "version": 1,
  "scenes": {
    "scenes/nonmq/Z2_CLOCKTOWER": {
      "name": "south_clock_town",
      "area": {"replaceable": 0.52, "unique": 0.41, "skip": 0.07},
      "screen": {"replaceable": 0.55, "unique": 0.40, "skip": 0.05, "measured": true},
      "n_tex": 90, "n_replaceable": 48, "n_unique": 30, "n_skip": 12
    }
  }
}
```

- `area`: share of the **world surface area** (static, from the display lists) by class group; available for every scene.
- `screen`: share of **on-screen pixels** measured in game (six yaw angles, 1080p, see the method README);
  only for the scenes that were measured (`measured: true`). The two agree on the scenes that have both (correlation in the method README).

## Reading the tables from C++

The tables are small (a few hundred KB each). Load with the project's JSON library at generation time. Keys are
resource paths; look up with the same string the archive manager reports. Nothing in them depends on the rendering path.
