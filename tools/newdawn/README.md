# tools/newdawn — New Dawn headless capture and comparison

`capture.py` (Python 3 stdlib only; needs `gamescope`, `gamescopectl`, ImageMagick `magick`/`montage`) boots the
game with no input, screenshots fixed spots, kills its own process tree and exits. Run from the repo root.

```sh
# 1. every named point (12, about 2.5 min), 1920x1080, from the frozen baseline
tools/newdawn/capture.py --bin-dir build-baseline --out /tmp/newdawn/before

# 2. the same against a new build, optionally with a setting changed
tools/newdawn/capture.py --bin-dir build-cmake/mm --out /tmp/newdawn/after --cvar gEnhancements.Graphics.DisableBlackBars=1

# 3. title sequence at given seconds after launch (no warp), alone or in addition to the points
tools/newdawn/capture.py --no-points --attract 20,48,76 --out /tmp/newdawn/attract

# 4. labelled side-by-side image per capture point (A left, B right)
tools/newdawn/capture.py --compare /tmp/newdawn/before /tmp/newdawn/after --out /tmp/newdawn/side
```

`--rot DEG` overrides a point's yaw (0 = +Z, 90 = +X; the camera starts behind the player), e.g. to look towards the sun. `--burst N,INTERVAL` takes N screenshots per point INTERVAL seconds apart (`<point>_00.png` ...), for motion checks. `--list` shows the points; `--points a,b` selects some; `--wait N` is the seconds from launch to the screenshot
(default 10; the scene is fully faded in after about 3). `--cvar name=value` is repeatable and wins over everything.

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

The default `materials.json` selects six visually inspected world textures: three South
Clock Town brick/paving surfaces, Termina Field dirt, Great Bay pale plaster and Ikana
rock. Debug captures confirmed the three Clock Town surfaces, the Great Bay texture (it lights the white plaster
building walls, not the beach), the Termina Field dirt request and Ikana layered rock as live draws; the
Termina Field dirt path itself was not found on screen in any tested view, so its look is unverified. Town brown paving and Ikana
layered rock replace two unused candidates from the initial offline manifest. Use `--manifest FILE` to supply
other canonical resource paths (without `alt/`). Each material's `overrides` accepts
`height_scale` (UV units), `normal_strength`, `invert` (height sign), and
`roughness_range: [lo, hi]` within `[0,1]`; `defaults` applies to every entry.

`--resolution 2` (default) averages 2x2 albedo pixels before generating half-axis maps;
`--resolution 1` retains full dimensions. `--stdlib` forces the slower fallback.
Outputs are `DIR/newdawn-materials.o2r` and `DIR/preview/*.png`, each showing
**albedo | normal | roughness | height**. Existing outputs are refused; choose a new
output directory or move the earlier outputs aside. Output inside this repository is
rejected. No game build or running game is needed.

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

Self-checks cover LE/BE TextureV1 round trips, malformed payload rejection, synthetic
bump normal directions, wrap filtering, flat normals, roughness bounds, and agreement
between stdlib and NumPy when available. Previews permit offline inspection only:
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
