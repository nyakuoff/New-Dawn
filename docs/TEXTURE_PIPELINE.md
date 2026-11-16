# Remastered textures: the texture pipeline (ND-016)

New Dawn **replaces** world textures instead of upscaling them. On the player's machine, at first launch, every texture the
classification tables call replaceable is regenerated from a small shipped library of CC0 PBR materials, recoloured to the
palette of the player's own N64 original and laid out at the original's UV scale. Unique painted art (murals, signs, carvings) cannot be
replaced by a generic material; it is upscaled instead (milestone 2, below), from the player's own 3DS texture where a safe counterpart
exists. Nothing generated is ever committed or shipped (ND-008).

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

## Milestone 2

**Base library (84 sets, 76.8 MB).** `tools/newdawn/matlib/build_library.py` builds `mm/assets/newdawn/materials/` from two inputs: `library.json` (17 hand picks the
tuned entries and recipe defaults name by id) and the curated shortlist of `tools/newdawn/texlib/catalog.json` (67 sets: rock, worked stone, planks, plaster, thatch, beams,
bark, roof tiles, dirt, gravel, sand, snow/ice, rope, mud, metal). JPEG at 1024 px: albedo q78 (4:2:0), normal q84 (4:4:4), roughness and height q68. Left out: brick, cobble and
flagstone (complete walls with their own joints), wallpaper, carpet, leather, fabric, moss and grass sources (weak in the catalog; the generator uses the moss and grass sets of the
hand picks or leaves the texture native). The two GPUOpen items of the shortlist are wallpaper (MIT), which is not imported, so no extra notice is shipped.

**Regions.** `recipes.json:scene_regions` maps 99 scene folders to a region priority list (`interiors,clock_town`); manifest sets carry `regions`; `Library::Resolve` picks, by seed,
among the sets tagged for the first matching region, then `any`, then the rest. Selectors may combine classes (`class:dirt|class:mud`); a selector that finds nothing leaves the texture native.
`world_m` (per class) repeats a set so one tile covers about that many metres of the surface (`scale.json:tile_m`, at most `max_repeat`), which fixed oversized sand and rock grain.
Grass is now a soft earthy base (dirt and mud sets, low relief): the visible grass is the 3D tuft pass. Fabric stays native.

**Unlit geometry** (`gNewDawn.Materials.Unlit`, `UnlitStrength`). Interiors and dungeons are vertex-coloured and unlit, so they had no normals and no maps. Unlit draws whose combiner multiplies
a texture by the vertex colour now bind the same companion maps: the vertex stage passes the world position only; the fragment stage derives a face normal from the screen derivatives of the position
(oriented toward the camera), builds the tangent frame from the UV derivatives, and shades the texture by how far the mapped normal turns away from the face normal against one fixed world light
(`(0.32, 0.76, 0.57)`, MM is Y up) plus the relief channel; the baked vertex colour then multiplies the result, so the room keeps its mood and its coloured light. A new shader variant (`o_unlit_material`);
a failed link is logged.

**Two map pairs per draw** (`gNewDawn.Materials.TwoLayer`). A multiplied two-texture draw (base times detail, e.g. the Termina wall) whose layers both have maps binds the second pair to slots 8 and 9, which the
OpenGL backend maps to texture units 22 and 23 (units 8 and up belong to postfx; those two slots never trust the bind cache). Shader option `MATERIAL_MAPS2` (the shader id shift moved from 23 to 24).
The fragment stage turns each pair into a world-space normal with its own tangent frame and combines them around the face normal, averages the roughness; parallax stays with the base.

**Unique art** (`UniqueArt.cpp`, `gNewDawn.Textures.Unique`, `UniqueScenes`). Textures the classification calls unique, or a material class below confidence 0.6, in the scenes of `recipes.json:unique.scenes`
(South Clock Town, Stock Pot Inn, Termina Field; `UniqueScenes=all` or a comma list overrides) are upscaled 4x with the CPU SPAN network of ND-017 and graded; textures with several palettes (`pal_alt`), intensity textures (I4/I8) and textures that cannot be decoded
stay native. The source is the player's 3DS texture when `mm3d_match.json` has a confident entry **and** it passes a coarse content test (aspect within 3 %, 2x to 4x the original's size,
mean colour within 45 levels per channel): the 3DS rooms are atlases and redrawn, so a name match alone would paste a whole atlas onto a wall (measured: of the 15 confident matches of these three scenes with a 3DS image, the
test accepts exactly the two that are visibly the same art; pixel correlation fails even on those). Otherwise the N64 original is the source. The 3DS textures are read straight from the RomFS scene files (`<scene>_info.zsi` and room files,
embedded CMB models, `Mm3d::ParseCmb`), only when `gNewDawn.MM3D.RomFS` points at a valid dump. Upscaling pads each side by 8 px, wrapping an axis whose edges meet (the texture tiles) and replicating otherwise. Grade:
the original's colour statistics (fully for 3DS sources, a third for N64 sources), a faint S-curve and +6 % saturation, and a faint grain from the plaster material of the region (`fine` channel, 3.5 %). No maps for unique art.

## Cvars

| cvar | default | |
|---|---|---|
| `gNewDawn.Textures.Remastered` | 0 (1 in Ultra, High, Medium) | use the generated textures |
| `gNewDawn.Textures.Generate` | 1 | developer switch: 0 skips generation and mounting |
| `gNewDawn.Textures.HeroSize` / `RestSize` / `HeroCount` | 1024 / 512 / 450 | tier sizes and count |
| `gNewDawn.Textures.Stochastic` | 1 | stochastic sampling of the flagged (isotropic) textures |
| `gNewDawn.Textures.Unique` / `UniqueScenes` | 1 / (recipes) | upscale unique art; `all`, a comma list of scene folders, or empty for the recipes' three scenes |
| `gNewDawn.Materials.Unlit` / `UnlitStrength` | 1 / 0.6 | material relief on unlit interiors and dungeons |
| `gNewDawn.Materials.TwoLayer` | 1 | both layers' maps on multiplied two-texture draws |
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

## Milestone 2 measurements (RX 7800 XT, 1080p, Ultra look, 2026-11-16)

Sheets (native | remastered, 1:1 crops at the most-changed windows) and numbers: `~/dev/ghm-mods/showcase/texture-pipeline-2/` (34 scene sheets, `changed_pixels.json`,
`unique_*.png`, `unique_3ds_sources.png`, `gpu_overlay_crops.png`). Day (8:00) and dusk (17:30); two views per scene except Snowhead Temple (day only).

| item | result |
|---|---|
| **Download** | base library **76.8 MB** (84 sets, all CC0); whole `assets/newdawn` 85 MB (milestone 1: 57.4 MB library) |
| **First run, default** (everything replaceable + unique art of 3 scenes) | 968 textures (872 materials + 96 unique), 42 skipped (transparency), **21 s** on 8 threads, **0.93 GB** cache |
| First run, unique art of **every** scene (`UniqueScenes=all`) | 2,299 textures (872 + 1,427 unique, 64 left native), **58 s**, **1.0 GB** |
| **GPU / CPU ms** (overlay, native to remastered) | Stock Pot Inn 3.11 to 3.18 / 1.41 to 1.68; Woodfall Temple 4.23 to 4.38 / 1.24 to 1.58; South Clock Town 5.07 to 5.45 / 2.30 to 2.86; Termina Field 6.49 to 6.75 / 1.98 to 2.04 (GPU +0.07 to +0.38 ms) |
| **GL errors** | 0 in all 68 matrix captures plus the GPU, identity, 3DS and all-scenes runs (Mesa messages and our link log searched) |
| **Off identity** | frozen Stock Pot Inn (an unlit scene, so the new path is covered), main with its own toggle off against this build with `Remastered=0`: mean abs difference 0.003 and 0.017 levels (main against main 0.029, ours against ours 0.049), no pixel above 8 |
| Brightness | mean luminance of the remastered frame over native: 0.96 to 1.07 in every scene sampled (the relief shading does not shift exposure) |
| Maps bound | Stock Pot Inn 3 and Woodfall Temple 7 materials now bind on the unlit path (milestone 1: 0); South Clock Town 13, Southern Swamp 5 |

Coverage. Textures generated per scene (default run; "of" = textures the scene owns): South Clock Town 21 materials + 45 unique of 87, Termina Field 25 + 32 of 84, Stock Pot Inn 6 + 19 of 38, Woodfall Temple 19 of 75, East Clock Town 14 of 78,
Southern Swamp 5 of 68, Great Bay Coast 18 of 45, Snowhead Temple 79 of 134, Mountain Village (winter) 7 of 19. Share of pixels that differ from native (RGB sum above 30 / above 6; day):

| scene | milestone 1 | milestone 2 |
|---|---|---|
| South Clock Town | 29-38 % / 51-64 % | **36-41 % / 75-80 %** |
| Termina Field | 48-49 % / 67-72 % | 50 % / 76-81 % |
| Stock Pot Inn | 14-17 % / 43-49 % | **19-26 % / 57-58 %** |
| Woodfall Temple | 1-3 % / 35-38 % | 3 % / 42-44 % |
| East Clock Town | 5-30 % / 11-50 % | 5-29 % / 11-50 % (unchanged: no unique-art scene) |
| Southern Swamp, Mountain Village (winter), Great Bay Coast | (not measured) | 50-65 %, 21-32 %, 9-13 % at above 30 |

Caveats of this metric: it includes actor and camera drift between two runs (the Inn view 60 at dusk read 19 % in milestone 1 because Link's pose differed; Snowhead Temple's camera moves between runs, so its 51-89 % is not usable) and it saturates on scenes the textures cover completely.

**What did not work, kept for honesty**
* The first unlit version rendered black room geometry: the vertex template used `@elseif`, which produced no `main`. Found by logging the program's link error.
* The first 3DS acceptance rule (aspect, 2x to 4x, mean colour) let two wrong textures through (a banner whose emblem sits on another flag, and a cut-out post that became an opaque wood rectangle). Fixed by requiring an opaque original and a coarse 8x8 colour layout test; of the 15 candidates now exactly the 3 correct ones pass. The rule is tuned on those 15 and could overfit; it is deliberately conservative.
* Moss class: the moss set alone looked like fur on vertical rock; the class is now rock with moss growth. Grass base: mud sets with ripples looked scalloped, and photo colour leaked through the chroma term (olive swamp ground); now fine grass and dirt sets with low chroma. The swamp ground is soft by design and blander than the native mottling.
* Recipe or algorithm changes used to need a manual `kVersion` bump (a cached archive was reused once after a rule change); since milestone 3 the version is a hash of the generator's sources.
* Frames of `Southern Swamp` and `Woodfall Temple` were recaptured after the grass/moss/chroma changes; the other scenes' frames predate them (only the grass, mud and moss classes changed; their textures are hidden or minor there).
* The 3DS-sourced textures (3, all in the Stock Pot Inn: the hexagon floor and the floral wallpaper) were checked as extracted pairs (`unique_3ds_sources.png`); they are not in any captured view.

## Milestone 3

**Cache safety.** `mm/CMakeLists.txt` hashes the generator's sources (`RuntimeTextures`, `TextureSynth`, `UniqueArt`, `SpanUpscale`, `Mm3dFormats`, `ZipWriter`, line endings normalised) at configure time; the files are
configure dependencies, so editing one re-runs CMake, and only `RuntimeTextures.cpp` is recompiled with `NEWDAWN_TEXTURE_LOGIC_HASH`. The stamp's `version` is `logic-<hash>`; the data files (`recipes.json`, tables), the library manifest, the SPAN
weights, the `mm.o2r` archive, the tier cvars and the unique-art settings were already part of the signature. A stale cache cannot be reused after any change of logic or data.

**Unique art everywhere.** `recipes.json:unique.scenes` is `"all"`: 1,427 textures of 100 scenes are upscaled (64 left native: several palettes, intensity textures, undecodable), 52.8 s and 1.1 GB for the whole generation on 8 threads.
**The 3DS acceptance rule failed a fresh sample.** Run over all 780 confident matches with a decodable pair it accepts 40 pairs from 17 scenes (the 3 Inn pairs plus 37 fresh ones). By eye: 13 clearly the same art, 14 clearly wrong (atlas pieces, a flat or noisy 3DS
texture over structured art, flowers turned into plain tile, a symbol turned into a plate), 13 the same kind of surface but not the same art. No pixel measure separated them (colour distance, 8x8 layout, normalised correlation at 16 px and at full size, high-pass correlation,
contrast ratio, black or transparent area: the 3DS textures are redrawn, so even correct pairs correlate near zero). So the rule cannot be validated automatically, and a 3DS texture is now used only for the **13 hand-verified pairs** listed in `recipes.json:unique.mm3d_verified`
(and only when the rule also accepts them). `gNewDawn.Textures.Unique3ds`: 0 off, 1 verified pairs (default), 2 every rule-accepted pair (experimental; about half of those are wrong).

**North Clock Town capture.** The point `north_clock_town` (entrance 0xd600, spawn 0 of Z2_BACKTOWN) boots with `gNewDawn.Debug.BootClockTownVisited=1` (capture/debug only, `WarpPoint.cpp`), which sets the three Clock Town entrance-cutscene flags the intro-skip enhancement also sets.
The earlier attempt reloaded the scene in a loop (first-arrival cutscene, and a spawn position of (0,0,0), outside the mesh).
