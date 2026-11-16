# Remastered textures: the texture pipeline (ND-016)

New Dawn **replaces** world textures instead of upscaling them. On the player's machine, at first launch, every texture the
classification tables call replaceable is regenerated from a small shipped library of CC0 PBR materials, recoloured to the
palette of the player's own N64 original and laid out at the original's UV scale. Unique painted art is not touched here
(milestone 2: upscale, MM3D-sourced where matched). Nothing generated is ever committed or shipped (ND-008).

```
 shipped (committed)                         player's machine (first launch, cached)
 ───────────────────                         ───────────────────────────────────────
 assets/newdawn/materials/   35 CC0 sets,    RuntimeTextures.cpp ──► cache/newdawn-textures/live/newdawn-textures.o2r
   manifest.json (+MANIFEST.md)  57 MB         for each replaceable texture:             alt/<resource>       albedo RGBA8
 2ship.o2r: newdawn_textures/                    decode the N64 original (+ its TLUT)     alt/<resource>_n     normal xy
   recipes.json, textures.json,                  palette ← quantiles of the original      alt/<resource>_r     roughness + relief
   classification/scale/layers.json              layout ← measured on its pixels          newdawn-materials.json  fingerprints,
                                                 TextureSynth: masonry | direct                 roles
 libultraship: remaster lookup, roles,         mounted after every mod; ResourceManager uses it when
   stochastic tiling                           gNewDawn.Textures.Remastered = 1 (independent of the mod switch)
```

## What the player sees

* Menu **Graphics > Advanced > Materials & Textures**: *Remastered Textures* (on in Ultra, High, Medium), status line, *Regenerate Remastered Textures*.
* First launch (and after the game data, the tables or the library change): a themed "Preparing remastered textures" popup, about 20 s on 8 cores; the result is cached.
* The community texture packs are untouched and stay the player's own choice (`gEnhancements.Mods.AlternateAssets`); with only *Remastered Textures* on, a pack's `alt/` entries are **not** used.

## Files

| file | what |
|---|---|
| `mm/assets/newdawn/materials/` | base library: `<id>.{albedo,normal,rough,height}.jpg` (1024², JPEG), `manifest.json` (id, class, roles, region, source, licence, hashes), `MANIFEST.md`. Built by `tools/newdawn/matlib/build_library.py` from `tools/newdawn/matlib/library.json`. |
| `mm/assets/custom/newdawn_textures/recipes.json` | recipe classes (generator, parameters, selectors), class map from the classification, tiers, scope |
| `.../textures.json` | hand-tuned entries (South Clock Town, Termina Field, Stock Pot Inn, Woodfall Temple, East Clock Town): class, seed, role, palette resource, parameter overrides |
| `.../classification.json`, `scale.json`, `layers.json` | from `tools/newdawn/texclassify` (see `README.md` there): class and confidence, UV area, base/detail/alt pairs |
| `mm/2s2h/NewDawn/TextureSynth.{h,cpp}` | the synthesis (C++ port of the round-2 spike; no engine dependency) |
| `mm/2s2h/NewDawn/RuntimeTextures.{h,cpp}` | first-run driver: tables, N64 decode, jobs, threads, cache, signature, mount, status |
| `tools/newdawn/matlib/synth_cli.cpp` | standalone driver for the synthesis (dev tool) |
| `libultraship` | `ResourceManager` remaster lookup; interpreter: roles, `LogDraws`, stochastic flag; `gfx_opengl` + `default.shader.glsl`: stochastic sampling |

## The generator

**Jobs.** Hand entries of `textures.json` first, then every texture of `classification.json` with `conf >= 0.6`, a class in
`recipes.json:class_map`, and a path under `scopes` (`scenes/`, `misc/scene_texture`). `unique`, `skip`, `water` and `foliage` are
left alone; textures with real transparency are skipped (counted). Characters/props (`objects/`) are out of scope for now.

**Decode.** All N64 formats (RGBA32/16, CI4/8 with the TLUT named by the table's `pal`, I4/8, IA4/8/16), exactly like the interpreter's
importers. I4/I8 are treated as opaque (the engine uses the intensity as alpha too; the replacement is opaque).

**Palette.** 24 luminance-quantile means of the original, +12 % saturation, smoothed into a 256-entry ramp. The generators only produce a
tone value, so every output stays inside the original's colour gamut: the colour identity is the original's, the structure is ours.

**Layout and scale.** Hand entries carry the cell counts (rows, columns, cobbles per tile, boards). For the others the generator
measures them on the original's pixels (circular autocorrelation of the row and column edge profiles; the classification does not provide
counts). Counts are used only above a periodicity score (rows 0.5, columns 0.25), otherwise the class default stays.
The output keeps the original's aspect and header layout (`h_byte_scale = W*4/(w*bytesPerTexel)`, `v_pixel_scale = H/h`), so UVs are untouched.

**Synthesis** (`TextureSynth.cpp`):

* `masonry` (brick, ashlar, cobble): periodic cell layout, hand-hewn edges (wobble + bites out of the arrises), per-cell window of a CC0 *face*
  set, CC0 *mortar* set, crevice grime, gravity streaks under the joints, edge wear, optional moss (CC0 moss set grown in cavities).
* `direct` (planks, grass, dirt, rock, plaster, ...): one CC0 set tiled onto the tile (planks: cropped between two plank gaps so the tile stays
  seamless), soft-clipped tone mapped onto the palette, macro tonal patches, cavity grime, wear, optional moss.
* Maps (`_n`, `_r`): the CC0 normals combined with the layout's chamfer; roughness from the CC0 set; relief (parallax height) from the
  band-passed height. Maps are stored at half the albedo size (like the HD-pack generator).

**Selectors.** `face`, `gap_set`, `set`, `moss_set` accept a material id or `role:<name>` / `class:<name>`: one manifest material by seed
(a region match first, then `any`). The library grows by adding entries to `library.json` (and, later, per-region variants), not code.

**Tiers and cache.** `tiers`: the biggest `hero_count` (450) replaceable textures by surface area (`scale.json`) are generated at 1024² (maps 512²),
the rest at 512² (maps 256²). Hand entries are hero. Archive: deflated zip, `cache/newdawn-textures/live/`, staging + signature (version, tables, library manifest,
tier cvars, `mm.o2r` size and time) like the materials generator.

**Memory.** About 120 MB of float fields per texture in flight (8 threads at boot, 4 for the background button) plus a library cache bounded to 768 MB.

## Resource lookup

`ResourceManager` has a *remaster* flag and the path of the generated archive. Alt lookup is active when mods **or** remaster are on;
with the mod switch off, `AltLookupAllowed(altPath)` is true only for entries that live in the generated archive (checked in `LoadResourceProcess`, `CheckCache` and the
interpreter's asynchronous alt loads). Material-map binding is unchanged: `alt/scenes/` albedos find their `_n/_r` and fingerprint in the same archive.
Toggling clears the texture cache.

## Layer roles

Draws that read two textures: `gNewDawn.Textures.LogDraws=1` logs each distinct pair (paths, combiner) as `TEXDRAW` lines. Findings (SCT, TF): the same texture on both tiles
(Clock Town bricks, a LOD alias, handled by the existing shared map), a base plus an unreplaced detail partner (Termina wall: plaster + block courses), two replaced layers.
`layers.json` gives the roles; the generator writes `role` into `newdawn-materials.json` (a texture that is a detail in every pair it is part of is `detail`). Both layers of a pair are
replaced in their own value range (the palette comes from each original, so a multiplied detail stays a modulation), and the interpreter lights a two-texture surface with the **base**
layer's maps when tile 0 is a detail layer. Limit: only one map pair is bound per draw (no blending of base and detail normals yet).

## Stochastic tiling

Isotropic classes (grass, dirt, rock, plaster, mud, moss, snow) are written with `TEX_FLAG_NEWDAWN_STOCH` in the albedo's flags. The interpreter passes one extra bit to the backend
(only for repeating tiles), the shader samples such textures with three taps on a triangle grid, each shifted by a hash of its grid vertex (albedo, normal and roughness/relief share the offsets).
Never applied to layouts (brick, planks, cobble): random offsets would break their courses.

## Cvars

| cvar | default | |
|---|---|---|
| `gNewDawn.Textures.Remastered` | 0 (1 in Ultra, High, Medium) | use the generated textures |
| `gNewDawn.Textures.Generate` | 1 | developer switch: 0 skips generation and mounting |
| `gNewDawn.Textures.HeroSize` / `RestSize` / `HeroCount` | 1024 / 512 / 450 | tier sizes and count |
| `gNewDawn.Textures.Stochastic` | 1 | stochastic sampling of the flagged (isotropic) textures |
| `gNewDawn.Textures.LogDraws` | 0 | log distinct texture pairs per draw |

## Extending

* **A material**: add it to `tools/newdawn/matlib/library.json` (id, asset, class, roles, region), rerun `build_library.py` (downloads, re-encodes, writes the manifest and MANIFEST.md, verifies the licence page), add the credit line to `docs/licenses/THIRD_PARTY_ASSETS.md`.
* **A texture's look**: add an entry to `textures.json` (class, seed, `p` overrides, `palette` for CI textures) or change the class recipe in `recipes.json`.
* **Region variants**: give manifest entries a `region`; `Library::Resolve` prefers them when the caller passes a region (not yet wired to scenes).

## Measurements and verification (RX 7800 XT, 1080p, Ultra look, 2026-11-16)

Sheets, raw numbers and the scripts' outputs: `~/dev/ghm-mods/showcase/texture-pipeline/` (20 native-versus-remastered sheets with 1:1 crops: South Clock Town, Termina Field, Stock Pot Inn,
Woodfall Temple and East Clock Town, each at 8:00 and 17:30, two views; `changed_pixels.json`, `gpu_crops.png`, `RESULTS.md`).

| item | result |
|---|---|
| **First run** | 917 textures planned (33 hand entries + 884 from the classification), **873 generated, 44 skipped** (real transparency), 0 without a palette, **20.0 s on 8 threads**, 1.34 GB archive (hero 1024² + maps 512², rest 512² + maps 256²) |
| Hand entries only (34) | 2.0 s, 76 MB (no scale-up needed for the tuned scenes) |
| Synthesis alone | 0.2-0.65 s per 1024² texture (3x the Python spike); peak about 120 MB per texture in flight |
| **GPU / CPU ms** (overlay) | South Clock Town 5.07 / 2.45 ms native, 5.47 / 2.85 ms remastered (+0.40 GPU); Termina Field 6.50 / 1.95 native, 6.87 / 2.36 remastered (+0.37 GPU). RAM +127 to +153 MB |
| **GL errors** | 0 in all 40 matrix captures and the identity runs (`MESA_DEBUG=context`, logs searched for GL errors) |
| **Off identity** | frozen Stock Pot Inn fixture (extended identity cvar list), main `b7a62e9fe` (play build) against this build with `Remastered=0`: mean absolute difference **0.000 and 0.006** levels in the two pairs (main versus main: 0.010, ours versus ours: 0.005; no pixel above 8 levels in any of them); toggle on: 2.47 mean, 8 % of pixels above 8 |
| Stochastic tiling A/B | on versus off on static geometry (Termina Field view 60): 0.13-0.58 levels mean difference: active, subtle in these views; no seams or artefacts seen |

**Coverage per scene** (share of the frame; "on screen replaceable" is the classification's own measurement over six views, "changed" is the share of pixels that differ between native and remastered in the two captured views, at a difference above 30 / above 6 summed over RGB levels; dark scenes need the lower threshold):

| scene | on screen replaceable / unique | textures replaced | changed (day, >30 / >6) | changed (dusk, >30 / >6) |
|---|---|---|---|---|
| South Clock Town | 32 % / 65 % | 22 of 87 | 29-38 % / 51-64 % | 16-17 % / 46-51 % |
| Termina Field | 45 % / 47 % | 26 of 84 | 48-49 % / 67-72 % | 18-37 % / 62-73 % |
| Stock Pot Inn | 39 % / 53 % | 6 of 38 | 14-17 % / 43-49 % | 11-19 % / 40-49 % |
| Woodfall Temple (mean brightness 26-30 of 255) | 88 % / 3 % | 21 of 75 | 1-3 % / 35-38 % | 2 % / 36 % |
| East Clock Town (stand-in for North) | 55 % / 35 % | 15 of 78 | 5-30 % / 11-50 % | 4-21 % / 9-46 % |

**Not verified / known limits**

* North Clock Town: its capture point lands in a static cutscene view on a fresh save (same as the texture spike), so East Clock Town stands in; NCT textures are generated by the same tables.
* Interiors and dungeons are unlit, vertex-coloured geometry: the new albedos apply but no material maps bind there (existing renderer rule), so the Inn and Woodfall gain colour and detail, not relief.
* Layout counts: measured counts agree with the hand-set ones on the Clock Town bricks (5 of 5) and partly on ashlar; planks, cobbles and the 16x8 brick textures fall back to class defaults.
* One map pair per draw: a base plus detail surface is lit by the base's maps only.
* Objects (actors, props) are out of scope; cut-outs (foliage) are skipped; the curated shortlist of `tools/newdawn/texlib` is not imported yet (the manifest and selectors are ready for it).
