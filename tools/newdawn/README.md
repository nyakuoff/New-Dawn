# tools/newdawn — New Dawn headless capture and comparison

`capture.py` (Python 3 stdlib only; needs `gamescope`, `gamescopectl`, ImageMagick `magick`/`montage`) boots the
game with no input, screenshots fixed spots, kills its own process tree and exits. Run from the repo root.

```sh
# 1. every named point (18, about 3.5 min), 1920x1080, from the frozen baseline
tools/newdawn/capture.py --bin-dir build-baseline --out /tmp/newdawn/before

# 2. the same against a new build, optionally with a setting changed
tools/newdawn/capture.py --bin-dir build-cmake/mm --out /tmp/newdawn/after --cvar gEnhancements.Graphics.DisableBlackBars=1

# 3. title sequence at given seconds after launch (no warp), alone or in addition to the points
tools/newdawn/capture.py --no-points --attract 20,48,76 --out /tmp/newdawn/attract

# 4. labelled side-by-side image per capture point (A left, B right)
tools/newdawn/capture.py --compare /tmp/newdawn/before /tmp/newdawn/after --out /tmp/newdawn/side
```

`--rot DEG` overrides a point's yaw (0 = +Z, 90 = +X; the camera starts behind the player), e.g. to look towards the sun. `--burst N,INTERVAL` takes N screenshots per point INTERVAL seconds apart (`<point>_00.png` ...), for motion checks. `--list` shows the points; `--points a,b` selects some; `--wait N` is the seconds from launch to the screenshot
(default 10; the scene is fully faded in after about 3). `--cvar name=value` is repeatable and wins over everything. `--preset` applies the New Dawn graphics preset read from `sNewDawnPresetCvars` in BenMenu.cpp, with the right int or float types; `--cvar` overrides it.

How it works / things to know:

- Each run gets a private `SHIP_HOME` under `<bin-dir>/captures/.run-*` (removed afterwards) holding hard links to
  the binary and archives plus a generated `2ship2harkinian.json`. Nothing in `<bin-dir>` is modified. The
  run dir also contains an empty `assets/` folder: without it the game shows an "Extractor assets not found"
  popup instead of starting (the dark grey frame).
- A point is a `WarpPoints` entry with `bootToPoint` (see `mm/2s2h/DeveloperTools/WarpPoint.cpp`); save is 8:00,
  day 1. Coordinates come from the scenes inside `mm.o2r`; each point's origin is in `--list`.
- `south_clock_town` needs the intro-skip cvars (see the comment above `POINTS` in `capture.py`); its save differs
  from the other points (human form, magic bar).
- The game and its gamescope are killed by PID with SIGKILL in a `finally` (also on Ctrl-C / SIGTERM) and checked
  gone; another agent's gamescope is never touched. Per-run game logs are in `<out>/logs/`.
- Warnings are printed at the end if a screenshot has the wrong size or is black/uniform; exit status is 1 then.
- Mods: every run gets its own empty `mods/`, so mods installed in `<bin-dir>/mods` are *not* loaded by default.
  `--mods DIR` links DIR's `.o2r`/`.otr` files into it and enables them in sorted filename order
  (`gSettings.EnabledMods`). Each run prints the archives the game actually loaded (`2ship.o2r`, `mm.o2r`, mods).

## Offline companion materials

`materials.py` generates private material hypotheses from a local, authorized ZIP-backed
`.o2r`. Python 3.9+ and stdlib suffice; an already installed NumPy accelerates generation
(no installation or downloads). TOML manifests need Python 3.11+; the starter manifest
is JSON. The tool follows this repository's CC0 licence; Python is PSF-licensed and
optional NumPy is BSD-3-Clause. These licences grant no rights to source or derived art.
Never commit or redistribute the input pack, companion archive, or previews.

```sh
python3 tools/newdawn/materials.py --self-check
python3 tools/newdawn/materials.py --pack /path/to/local-pack.o2r --out /tmp/nd/materials
```

The default `materials.json` keeps six calibrated per-texture overrides and nine
material-class recipes. Generate coverage for the whole local pack with:

```sh
python3 tools/newdawn/materials.py --pack /path/to/MM_Reloaded_v11.0.3_4K.o2r \
    --classify tools/newdawn/material-classification.json --jobs 4
python3 tools/newdawn/materials.py --pack /path/to/MM_Reloaded_v11.0.3_4K.o2r \
    --classification tools/newdawn/material-classification.json \
    --out /home/loganp/dev/ghm-mods/materials/full --jobs 8
```

Classification requires an already installed NumPy. Generation also has the stdlib
fallback (`--stdlib`). Only `alt/scenes/` TextureV1 resources are candidates: actors,
UI, fonts, skyboxes and particle resources outside scene paths are excluded. Within
scenes, reject short axes below 128, areas below 65,536 pixels, nonopaque pixels and
near-flat colours. This deliberately excludes opaque surfaces packed together with
cutouts as well as foliage. It does not infer which textures are actually drawn.

The classification JSON records every scene candidate, inclusion/exclusion, class,
confidence, dimensions, hue, saturation, high-frequency energy and periodic-border
statistics. Scene-name priors and directional grain refine the colour rules;
poor tileability lowers confidence. Numeric resource names contain little semantic
information: wood/stone, sand/plaster, snow/metal and painted signs/roof tiles can be
confused. Confidence is a heuristic score, not a calibrated probability. These are
reviewable hypotheses, not authored physical materials. `other`, grass and plaster
use particularly mild relief. Metal remains rough to avoid speculative mirror-like
surfaces. Transparent water is excluded; `wet` means an opaque coastal/dungeon candidate.

Recipes merge built-in defaults → manifest defaults → class defaults → per-texture
`overrides`. The original six overrides win over automatic classes. To correct a
classification, edit its `class` (or `excluded`), or add a manifest entry with explicit
`overrides`. `height_scale` is in UV units; `normal_strength`, `invert` and
`roughness_range: [lo, hi]` retain their existing meanings. Running without
`--classification` generates just the explicit manifest entries.

`--resolution 2` (default) averages 2×2 albedo pixels before generating half-axis maps;
`--resolution 1` retains full dimensions. Half axes use one quarter of the uncompressed
map bytes and reduce high-frequency noise, at the cost of fine relief. The full source
fingerprint still authenticates the original albedo. Wrap filters avoid introducing
filter-edge discontinuities; a source painted with mismatched borders can still seam.

Outputs stay outside the repository: `DIR/newdawn-materials.o2r`,
`DIR/generation-report.json`, and a persistent `DIR/cache/` of compressed per-source
maps. Cache keys cover the full source SHA-256 (including header), recipe, generator
version and resolution. Unchanged entries skip fingerprinting and map generation;
identical source/recipe pairs deduplicate. Changed archives are staged and atomically
replaced after every entry succeeds. Entirely unchanged runs leave the archive untouched.
Old cache entries may be deleted when no generator is running; doing so only costs
regeneration time. Use one generator per output directory. Bulk generation omits
previews to avoid multiplying disk use. Offline maps alone do not prove visual quality.

Install the companion **after** the albedo archives. The local `sets/full` contains
symlinks to the existing packs; leave those links alone and copy the generated archive
as `sets/full/zz-newdawn-materials.o2r`. Never commit the archive or cache.

Binary contract, verified against this fork's `ResourceLoader` and
`ResourceFactoryBinaryTextureV1` in `libultraship`:

- A 64-byte resource header declares byte order, custom flag, type `0x4F544558`
  (`OTEX`) and version 1. Body fields are `u32 type, width, height, flags`,
  `f32 HByteScale, VPixelScale`, `u32 payload size`, then bytes. Both byte orders
  decode; output is little endian. Malformed sizes and dimensions fail clearly.
- The inspected MM Reloaded v11.0.3 4K pack contains 10,007 TextureV1 entries,
  all `flags=1` (`LOAD_AS_RAW`), with type labels 1, 2, 5, 6, 7, 8, 9. These are
  expanded **RGBA8** regardless of the original N64 type label, matching
  `ImportTextureRaw`. The tool also accepts their `LOAD_AS_IMG` equivalents and
  unflagged native RGBA32; palettes and other layouts fail rather than guessing.
- `alt/<resource>_n` and `_r` are TextureV1 type 9 (IA16 container), flags 0,
  scales 1, storing two tightly packed **linear RG8** bytes per texel. `_n` holds
  normal X/Y; `_r` holds roughness/height. The material uploader reads RG8 directly, bypassing the
  ordinary IA-to-RGBA albedo expansion.
- Decode normal XY as `RG/255*2-1`, reconstruct positive Z with
  `sqrt(max(0,1-dot(XY,XY)))`, then normalize. Convention: +X right, +Y up,
  +Z outward, while image rows increase downward. Preserve albedo alpha separately.
- Height combines softened linear Rec.709 luminance at three scales, with broad
  lighting removed; Sobel derivatives and every box filter wrap across borders.
  Roughness maps bounded local standard deviation into the requested range.
  `newdawn-materials.json` inside the archive records generator version, recipes,
  per-entry source SHA-256 (including header), dimensions, scales and full-image
  crop/UV metadata. The runtime also checks `source_pixels_fnv1a64` (FNV-1a of decoded full-resolution
  RGBA8), dimensions and source scales against the winning albedo, on a background worker.
  Regenerate archives made before this fingerprint was added; stale/mismatched maps are skipped.

Self-checks cover incremental source/recipe cache invalidation, LE/BE TextureV1 round trips, malformed payload rejection, synthetic
bump normal directions, wrap filtering, flat normals, roughness bounds, and agreement
between stdlib and NumPy when available. Offline inspection has limits:
painted shadows and colour variation can create false relief, and no in-game visual
quality claim follows from generation alone. The generator changes no gameplay or animation data.


## Loading and lighting materials

Drop the generated `newdawn-materials.o2r` into the game's `mods/` directory and enable it
in the mod menu, after the albedo pack. It is an ordinary mod archive; no new loader or
external asset directory is needed. Enable **New Dawn → Lighting → Companion Materials**
and per-pixel lighting. The New Dawn preset enables both. `gNewDawn.Materials.Enabled`
defaults to 0 outside the preset; `gNewDawn.Materials.Strength` defaults to 1 (0 restores
original shading, range 0–2). The effect works in HDR and LDR on OpenGL.

`capture.py --mods` accepts one directory and uses a private mod list. For full-pack plus
companion captures, make a small directory of symlinks (the following only links local files):

```sh
mkdir -p /tmp/nd/matbind/mods
for f in /home/loganp/dev/ghm-mods/sets/full/*.o2r; do
    ln -s "$f" /tmp/nd/matbind/mods/
done
ln -s /path/to/generated/newdawn-materials.o2r /tmp/nd/matbind/mods/zz-newdawn-materials.o2r
python tools/newdawn/capture.py --bin-dir build/mm --mods /tmp/nd/matbind/mods \
    --points south_clock_town --rot 180 --out /tmp/nd/matbind/on \
    --cvar gEnhancements.Mods.AlternateAssets=1 \
    --cvar gNewDawn.Lighting.PerPixel=1 --cvar gNewDawn.Materials.Enabled=1
```

Add all `sNewDawnPresetCvars` values from `mm/2s2h/BenGui/BenMenu.cpp` for a full-preset
comparison; the command above only demonstrates loading. Change Materials.Enabled to 0
for the matching off image. Archives actually loaded are printed by the capture tool.

Only supported opaque, lit world surfaces use maps: no rectangles, environment-generated
UVs, masked/blended replacements, framebuffer textures, custom shaders or translucent draws.
The renderer supports a separable texture×shade combiner, its primitive tint, primary
albedo×detail, and two-albedo blends/LOD aliases. A companion follows its own tile's UV,
clamp, repeat and mirror state; a real blend weights relief/highlights by that tile's
contribution. If both different albedos have companions, one pair is selected (tile 0 wins);
this foundation does not evaluate two full material lobes per pixel.

Companion reads share the existing asynchronous resource pool. Flat shading continues
while reads and provenance checks are pending. The existing synchronous rule for mod-only
**albedo** remains intact. Hits and misses are cached per winning albedo until texture-cache
invalidation; invalidation after a mod change is required. The RG8 maps use units 6/7,
mipmaps and the same LRU GPU byte budget as albedo. Units 8+ remain for scene effects.
There is no parallax, displacement or asset redistribution.

`gNewDawn.Materials.Debug.Stats=1` optionally times all OpenGL draw batches with deferred
GPU queries, equally with materials on/off. Compare the same scene, view, preset, resolution
and GPU. Logs report ms/frame averaged over 300 output frames and any untimed draws; these
exclude post-processing and presentation. Do not infer material cost from post-fx timers.

Shading: a supported surface keeps the game's own per-pixel diffuse model (so tone, shadows and
HDR behaviour match the surrounding scene) but evaluates it on the map-perturbed normal, then adds
a GGX/Smith/Schlick dielectric specular term from the same light set, with roughness from the
map widened by minification. Strength scales relief (0-2) and specular (capped at 1). With
Enabled=0 frames were pixel-identical to a build without this feature in the three scenes tested. The New Dawn preset
uses Strength 1.5.

## Coverage validation — 2026-11-13 (local assets only)

MM Reloaded v11.0.3 4K: 3,200 scene textures across 100 scenes; 2,209 selected,
932 alpha-bearing and 59 small textures excluded; 6,807 resources outside the scene
texture scope. Selected classes: stone 1,593; plaster 247; grass 195; other 115;
dirt 30; wet 10; roof tile 10; wood 8; metal 1. Class labels remain heuristic.

Eight workers generated the first pass in 92.8 s. After inspecting class samples and
in-game images, roof/sign relief was reduced, then automatic stone height was reduced
from .012 to .004 and dirt from .006 to .003. The final tuning pass regenerated 1,162
entries in 188.54 s (including archive assembly). An unchanged full run reused all
2,209 entries in 8.77 s. The installed half-axis archive is **1,024,776,857 bytes**
(977.3 MiB), under the 1.5 GB target. Full-axis maps would quadruple uncompressed
map storage; their compressed size and visual benefit were not measured.

Validation commands run: `cmake --build build -j16` (current), `materials.py
--self-check`, `materials.py --stdlib --self-check`, and `python3 -m py_compile
tools/newdawn/materials.py`. Generation/classification commands are above. Local
capture drivers `/tmp/nd/matcov/captures.py` and `motion.py` apply every current
`sNewDawnPresetCvars` value, then call `capture.py --bin-dir build/mm --mods
/home/loganp/dev/ghm-mods/sets/full --cvar gEnhancements.Mods.AlternateAssets=1`.
All 12 points were captured with materials off and on, then on again after tuning.
Comparisons: `/tmp/nd/matcov/final-compare/<point>.png` (off left, on right).
The first-pass pairs remain in `/tmp/nd/matcov/compare/`.

**Pale-patch regression fixed (shader).** The grey/white wash had two causes in
`materialSpecular`: a negative N·V was clamped positive (specular on back-facing
perturbed normals), and a broad GGX lobe was added over dark painted albedo. The shader
now returns no specular for back-facing N·V/N·L, fades the lobe by smoothness squared, and bounds it per
channel relative to the lit texel. Stone relief was reduced (class `height_scale` 0.001,
Clock Town paving 0.003). `gNewDawn.Materials.Debug.View` (1 specular, 2 roughness, 3 N·V sign,
4 diffuse only) aids diagnosis. Over all 12 points x 32 regions, materials on stays within
1.9% mean luminance and 1.3% RMS contrast of off. Comparisons: `/tmp/nd/matcov/attempt2-compare/`.

Walking bursts (`--burst 8,0.35`) at Clock Town and Ikana are in
`/tmp/nd/matcov/motion-{on,off}/`: the private Xwayland display receives W for five
seconds to produce real player/camera motion, without changing gameplay code or saves.
The first-pass bursts exposed the pale patches fixed above; the fixed bursts (`motion-a2on`) show none. Full video,
all-texture normal orientation and every source tile border are not certified by this
limited sample. Painted shadows, ambiguous classes, opaque foliage, unsupported
runtime draw combiners and alpha-bearing texture atlases remain limitations.

At 1920×1080, MSAA 4, full preset, RX 7800 XT, matched 20-second runs with the
same installed archives gave these steady draw-batch GPU times (last two 300-frame
samples averaged; excludes post-processing/presentation):

| Scene | Off | On | Difference |
| --- | ---: | ---: | ---: |
| South Clock Town | 0.366 ms | 0.491 ms | +0.125 ms |
| Termina Field | 0.332 ms | 0.421 ms | +0.090 ms |
| Ikana Canyon | 0.348 ms | 0.493 ms | +0.145 ms |
| Stone Tower Temple | 0.330 ms | 0.386 ms | +0.056 ms |

Raw samples: `/tmp/nd/matcov/perf-{off,on}/logs/`; summary:
`/tmp/nd/matcov/metrics.json`. The boot archive-to-first-scene log marker varies
from roughly 0.4–1.0 s across runs, including with materials off. It is not a
scene-transition/load timer; a causal scene-load cost difference remains unmeasured.
Companions become ready asynchronously after scene entry. No GL or null-texture
errors were found in the 12-point off/on/tuned sweeps or first walking bursts.

Additional 180° views of Clock Town, Termina and Ikana were captured off/on and
inspected in `/tmp/nd/matcov/sun-compare/`; they reproduce the same pale patches.
Tuned walking bursts are in `/tmp/nd/matcov/motion-final/`. They confirm reduced
coarse ground relief but retain the view-dependent wall patches; this is not a
clean shimmer signoff. Clock Town's W path crosses into East Clock Town, and frame
`06` triggered the capture tool's uniform-image warning during the transition;
that frame is unsuitable for assessing materials. No extra isolated sparkling or
obvious inverted grooves were identified in the other sampled frames, but the
existing lighting regression prevents visual approval. The final log scan covers
56 runs (12 off, 12 first on, 12 tuned on, 8 performance, 6 rotated and 6 burst runs)
with zero logged GL/null-texture errors. No gameplay, animation, engine or shader
source was modified; interactive gameplay regression coverage remains limited to
these private capture runs.

## Art-directed albedo recolour

`recolor.py` applies the explicit environment-only shortlist in `recolor.json` to a local HD archive.
It requires NumPy; `--preview` also needs Pillow. Source and derived art must remain private and outside
this repository. See [ART_DIRECTION.md](../../docs/ART_DIRECTION.md) for recipes, local commands,
archive order, visual evidence and limitations.

```sh
python3 tools/newdawn/recolor.py --self-check
python3 tools/newdawn/recolor.py --pack /path/to/4K.o2r --base /path/to/mm.o2r \
    --out /private/mods/zy-newdawn-recolor.o2r
```

Regenerate material companions from the final albedo: add
`--overlay-pack /private/mods/zy-newdawn-recolor.o2r` to the existing `materials.py` generation command.
Repeat `--overlay-pack` in archive load order if there are several overrides; the last matching entry wins.
Unchanged source content reuses the existing material cache. Old companions for changed albedos fail the
renderer's source fingerprint check.
