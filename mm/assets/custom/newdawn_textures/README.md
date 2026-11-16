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

> Version 1, generated 2026-11-16 from the maintainer's `mm.o2r` (50,496 entries) and *Majora's Mask 3D* RomFS dump. Fields marked
> *(optional)* may be absent. Counts and caveats are in `docs/TEXTURE_CLASSIFICATION.md`.

## Files

| file | content |
|---|---|
| `classification.json` | one entry per world/character texture: material class, confidence, review state |
| `scale.json` | UV density and world size per texture |
| `layers.json` | per two-texture draw: which texture plays which role |
| `mm3d_match.json` | for `unique` scene textures: the matching *Majora's Mask 3D* texture name, with scores |
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
      "pal": "scenes/nonmq/Z2_CLOCKTOWER/Z2_CLOCKTOWERTLUT_022620"
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
- `pal_alt` *(optional)*: further TLUTs the texture is drawn with (a texture can be drawn with several palettes).
- Not provided: cell counts per tile (rows and columns of bricks, planks, cobbles). The autocorrelation estimate did not agree with the
  hand-set counts of the stylelib spike often enough to ship; the generator should measure them itself on the original pixels.

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

Draws that read **two different textures** (RDP tile 0 `a` and tile 1 `b` both used by the colour combiner). Draws that read one
texture, or the same texture twice, are not listed (use `classification.json` and `scale.json`). 809 pairs.

```json
{
  "version": 1,
  "roles": ["base", "detail", "decal", "alt", "lod_dup"],
  "patterns": {"modulate": "...", "lerp_prim": "...", "lerp_lod": "...", "lerp_alpha": "...", "other": "..."},
  "pairs": [
    {
      "a": "scenes/nonmq/Z2_00KEIKOKU/Z2_00KEIKOKUTex_01EE50",
      "b": "scenes/nonmq/Z2_00KEIKOKU/Z2_00KEIKOKUTex_01E650",
      "pattern": "modulate",
      "roles": ["base", "detail"],
      "tpu_a": [0.1194, 0.0889],
      "tpu_b": [0.2388, 0.7112],
      "scenes": ["scenes/nonmq/Z2_00KEIKOKU"],
      "area": 12000800,
      "alpha": "opa",
      "cc": ["(TEXEL1-0)*TEXEL0+0", "(COMBINED-0)*SHADE+0", "(0-0)*0+TEXEL0", "..."]
    }
  ]
}
```

- `roles` = `[role of a, role of b]`:
  - `base`: carries the surface colour; its class decides the material.
  - `detail`: a tiling overlay multiplied on (courses, grime, mottling). Replace the pair by the replacement of the `base` and treat
    `detail` as a modulation (or drop it when the replacement carries its own).
  - `decal`: a `unique`/alpha overlay over a base; do not replace, upscale.
  - `alt`: the second variant of two alternates blended by the primitive LOD fraction (`lerp_prim`, the most common pair: 294 of 809);
    both are the same surface, replace both with the same material.
  - `lod_dup`: the pair is a mip blend of one image; use `a` alone.
- Role assignment is heuristic for `modulate`/`other` (the more saturated, smoother texture is `base`, the greyer, higher-contrast one
  `detail`; a `unique` texture is `decal`, a `skip` texture `detail`). Pairs drawn from the same scene state are exact (taken from the display
  lists); roles are a judgement. `cc` holds the four combiner equations (colour cycle 0, alpha cycle 0, colour cycle 1, alpha cycle 1).
- `tpu_a`, `tpu_b`: texels per world unit per tile, including the tile's own shift (tile 1 usually has a different scale).
- `alpha`: `opa`, `cutout` or `xlu` render mode of the draw. `area`: surface area summed over the scenes that draw the pair.

## `mm3d_match.json`

Only for `unique` **scene** textures (`scenes/...`). Characters and actors are not matched. **Names and numbers only; nothing from the
RomFS is stored.** The 3DS rooms are redrawn (different layout and detail, atlased textures), so pixels do not match; the correspondence is
found by **geometry**: the same wall occupies the same place in both games (same coordinate system and scale), so each N64 triangle drawn
with the unique texture is paired with the 3DS triangle at that position (normal within 37 degrees, within 16 units) and votes for the 3DS
texture that triangle uses.

```json
{
  "version": 1,
  "coverage": {"unique_scene_textures": 1484, "with_candidate": 1159, "confident": 874, "by_count": {"candidate": 0.781, "confident": 0.589}, "...": "..."},
  "textures": {
    "scenes/nonmq/Z2_CLOCKTOWER/Z2_CLOCKTOWERTex_01FB80": {
      "scene": "z2_clocktower", "mm3d": "clsouth_tower_0", "score": 1.0, "margin": 1.0, "share": 1.0, "coverage": 1.0
    }
  }
}
```

- `scene`: MM3D scene name (`tools/newdawn/mm3d/scene-table.json`). `mm3d`: the texture's **name** in that scene's room meshes (the 16-character
  name from the CMB texture table). It means nothing without the player's dump; the local tool resolves it there. 3DS textures are
  **atlases**: one name covers many tiles, so the correspondence is texture-level (the UV rectangle is not recorded).
- `share`: area share of the winning 3DS texture among the triangles that found any counterpart. `coverage`: fraction of the N64
  texture's surface that found a counterpart. `score = share * min(1, coverage / 0.5)`. `margin = share - runner-up share`.
- **Use an entry when it has no `weak` flag**: `share >= 0.6`, `coverage >= 0.3` and `margin >= 0.2`. `weak: true` entries
  (`share >= 0.4`, `coverage >= 0.1`) are only hints: a big N64 wall often corresponds to several 3DS textures, and the vote splits.
- A unique texture with no entry has no usable 3DS counterpart: upscale the N64 original. `coverage` at the top holds the overall figures.

## `scene_stats.json`

Per scene folder (`scenes/nonmq/<SCENE>`, 100 scenes):

```json
{
  "version": 1,
  "scenes": {
    "scenes/nonmq/Z2_CLOCKTOWER": {
      "name": "South Clock Town",
      "area": {"replaceable": 0.59, "unique": 0.4, "skip": 0.0},
      "xlu_area_fraction": 0.03,
      "n_tex": 87, "n_replaceable": 31, "n_unique": 47, "n_skip": 9,
      "screen": {"replaceable": 0.32, "unique": 0.64, "skip": 0.03, "measured": true, "capture": "south_clock_town", "views": 6}
    }
  }
}
```

- `area`: share of the scene's **opaque and cut-out surface area** (static, from the display lists, a draw counts once, by its `base`
  texture) by group: `replaceable` = a material class with `conf >= 0.6`; `unique`; `skip`. Translucent draws are excluded and reported as
  `xlu_area_fraction` (share of the scene's total area). Available for every scene.
- `screen` *(10 scenes only)*: share of **on-screen world+object pixels** measured in game with a texture-ID buffer (six yaw angles, 1080p,
  New Dawn off; the texture spike). Includes actors and the shared `misc/scene_texture_*` textures. The two measures differ because area is
  not visibility: use `screen` where it exists.
- `n_*`: texture counts of the scene folder (shared `misc/` and `objects/` textures are not counted here).

## Reading the tables from C++

The tables are small (a few hundred KB each). Load with the project's JSON library at generation time. Keys are
resource paths; look up with the same string the archive manager reports. Nothing in them depends on the rendering path.
