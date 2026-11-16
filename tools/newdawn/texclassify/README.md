# texclassify: how the texture tables are made

Produces `mm/assets/custom/newdawn_textures/*.json` (names and numbers only; schema in that folder's README, results in
`docs/TEXTURE_CLASSIFICATION.md`). Python 3 + numpy + Pillow; CLIP is dev-time only (torch CPU + `open_clip_torch`, MIT licences, in a venv
outside the repository). **Everything it reads is the maintainer's own dump** (`mm.o2r`, the *Majora's Mask 3D* RomFS); the intermediate
files (decoded textures, embeddings, contact sheets) are game-derived and stay in a work directory outside the repository.
Heavy steps go through `nd-heavy tool --` (one at a time, at least 8 GB free).

## Pipeline

| step | tool | what it does | time |
|---|---|---|---|
| 1 | `walk.py --out walk.jsonl` | **Static F3DEX2 interpreter.** Reads every display list of `scenes/`, `objects/`, `misc/` straight from `mm.o2r` (resource paths are CRC64 hashes, `o2r.py` rebuilds the table from the archive's file names), follows `G_DL` calls with inherited RDP state (SETTIMG, SETTILE, LOADBLOCK/LOADTLUT, SETTILESIZE, TEXTURE, SETCOMBINE, other modes) and records per draw state: tile-0 and tile-1 texture and TLUT, combiner, wrap and shift of the tiles, alpha mode, envmap flag, surface area and texel density (area-weighted median of texels per world unit from each triangle's UV gradient). 7,360 to 8,155 draw states. Replaces in-game logging: it sees every draw of every scene, not just the ones a capture reaches (all 683 textures of the texture spike's ID-buffer captures are found). | 25 s |
| 2 | `features.py` | decodes all 7,527 non-TLUT world/character textures with the palette they are drawn with (area-weighted TLUT; 242 CI textures never drawn through a display list decode as grey) and computes numeric features (colour/hue families, entropy, edge energy, anisotropy, seam, symmetry, periodicity, usage: clamp/wrap, xlu, cut-out, envmap). | 6 s |
| 3 | `clip_embed.py`, `clip_text.py`, `classify.py` | zero-shot CLIP prior over the class prompts (`prompts.py`) on the single image and on a 2x2 tiled view, combined with usage evidence (clamped decals, translucent, envmap, cut-outs) and object-name hints (eyes, mouths, flames...). This is only the **suggestion**. | 5 min |
| 4 | `cluster.py` | k-means on the CLIP embeddings of the **review head** (the textures that carry 99.5 % of scene and 99 % of object visible surface, all `misc/`, plus the translucent-only ones: 3,891) and renders 26 contact sheets, cells ordered by cluster. | 1 min |
| 5 | by hand | **Visual review.** Every cell of every head sheet was looked at; decisions are typed as `cN=class` (cluster default, or a family `@earth`/`@stone` that keeps the automatic label inside the family) and `idx=class` (exceptions) and merged by `apply_review.py`. Rule: unique art and anything unclear is `unique` (a wrongly replaced unique texture is worse than a missed replacement). | hours |
| 6 | `propagate.py`, `tailcheck.py` | the 3,636 tail textures take the label of their nearest reviewed neighbours (weighted vote) only when it agrees with the automatic label or the vote is unanimous and very close; otherwise `unique` (or `skip` when both say so). All 333 tail entries that ended up as a material were looked at on spot-check sheets and corrected. Unlabelled tail `unique`/`skip` entries keep `rv: 0`. | seconds |
| 7 | `pass2.py` | **second review** of the textures that dominate the measured screen (top 36 per measured scene, 324 cells, larger cells, with their screen share): corrected the East Clock Town brick, a few mislabelled thatch/window strips, tree billboards (unique), Ikana rubble (rock). | |
| 8 | `finalize.py` | merges head + tail + pass-2 + late corrections into `final.json` (`labels.json` here is its reviewed part, names and classes only). | |
| 9 | `export.py` | writes the committed tables: `classification` (class, confidence, review flag, palette), `scale` (texel density per texture, from the walk), `layers` (two-texture draws with pattern and roles, from the combiner), `scene_stats` (area shares per scene from the walk; screen shares for the 10 measured scenes from `measured.py`), `mm3d_match`. | seconds |
| 10 | `match3ds.py` | N64 -> MM3D correspondence for unique scene textures **by geometry** (see below). | 3 min |
| 11 | `classsheets.py`, `check.py` | per-class contact sheets of the final labels; schema self-check of the committed tables. | |

`measured.py` condenses the texture spike's per-capture ID-buffer TSVs into per-scene screen shares (names and shares). Those captures came
from a test-only ID buffer in the interpreter (not committed; see the texture spike write-up).

## Layer roles

`export.py` classifies the combiner of every draw that reads TEXEL1: `modulate` (TEXEL1 x TEXEL0), `lerp_prim` (blend of two variants by
the primitive LOD fraction), `lerp_lod` (mip blend), `lerp_alpha` (decal over base), `other`. Roles follow from the pattern and the classes of
the two textures (see the schema). 809 distinct pairs; 294 are `lerp_prim`.

## MM3D correspondence

3DS rooms are **redrawn atlases**: pixel template matching against the atlases (tried first, `NCC`, 8 orientations, 3 scales) found no genuine
counterpart (best scores were look-alike bricks with zero margin). The 3DS geometry is in the same coordinate system and scale as the N64
rooms, so `match3ds.py` instead pairs every N64 triangle drawn with a unique texture with the 3DS triangle at the same place (normal within
37 degrees, within 16 units; spatial hash over four sample points per 3DS triangle) and votes by area for the 3DS texture that triangle uses.
`share`, `coverage` and `margin` are defined in the schema README. Scene textures only; actors are not matched.

## Regenerating

```sh
W=~/dev/ghm-mods/work/tex-classify   # any directory outside the repository
nd-heavy tool -- python3 -I tools/newdawn/texclassify/walk.py --out $W/walk_all.jsonl
python3 -I tools/newdawn/texclassify/measured.py <texture-spike>/cov $W/measured.json     # optional, 10 scenes
nd-heavy tool -- python3 -I tools/newdawn/texclassify/features.py --walk $W/walk_all.jsonl --work $W
nd-heavy tool -- $VENV/bin/python tools/newdawn/texclassify/clip_embed.py $W && nd-heavy tool -- $VENV/bin/python tools/newdawn/texclassify/clip_text.py $W
python3 -I tools/newdawn/texclassify/classify.py $W                      # auto.json (the suggestion)
# labels.json (this folder) holds the reviewed labels; cluster.py + review are only needed to review new textures
python3 -I tools/newdawn/texclassify/propagate.py $W <reviewed labels json> $W/final0.json
python3 -I tools/newdawn/texclassify/finalize.py $W $W/final0.json <tail_decisions> [pass2_decisions] [late]   # -> final.json
nd-heavy tool -- python3 -I tools/newdawn/texclassify/match3ds.py $W $W/final.json <mm3d romfs> $W/m3.json
python3 -I tools/newdawn/texclassify/export.py $W mm/assets/custom/newdawn_textures
python3 -I tools/newdawn/texclassify/check.py
```

`ND_MM_O2R` overrides the default `~/dev/2ship2harkinian/build-cmake/mm/mm.o2r`. The review itself is a human step: `labels.json` is
its record (it is what the tail propagation and any rerun start from).
