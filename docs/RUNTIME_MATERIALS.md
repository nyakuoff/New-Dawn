# Runtime material maps and texture recolour

New Dawn's normal, roughness and relief maps (#31/#33/#34, used by per-pixel lighting, GGX and parallax, #57) and the
offline albedo recolour (#51) are built from the game's own HD textures. Per ND-008 they are never committed or shipped,
so the game generates them **on the player's machine**, like 2Ship extracts its assets from the ROM. Players need no
Python.

## What the player sees

1. First launch with an HD texture pack installed: a themed "Preparing New Dawn" popup with a progress bar (about 9 s,
   see Measurements). The result is cached.
2. Every later launch: a signature comparison (a few stat calls), no popup.
3. **Graphics → Advanced → Materials & Textures**: a status line (`Generated: 2209 material maps, 42 recoloured textures`,
   `Generating material maps... 61% (1364 of 2209 textures)`, `No HD texture pack is loaded...`, `Generation failed: ...`,
   `Missing...`, `Disabled`) and a **Regenerate Material Maps** button. The button rebuilds in the background (half the
   cores, game keeps running) and the new maps apply on the next launch (`Regenerated 2209 maps in 11s. Restart the game to
   use them.`). The widget is also found by searching "regenerate".

## Design

| Piece | Where |
| --- | --- |
| Recipes and classification (our data, not game data) | `mm/assets/custom/newdawn_materials/{materials,material-classification,recolor}.json`, packed into `2ship.o2r` (read as `newdawn_materials/*.json`) |
| Generator | `mm/2s2h/NewDawn/RuntimeMaterials.{h,cpp}`: C++ ports of `materials.py` (numpy path, half-axis maps) and `recolor.py` |
| Boot hook and popup | `RunMaterialsBoot()` in `mm/2s2h/BenPort.cpp`, after `ModMenu_LoadArchives()` |
| Settings widget | `RenderRuntimeMaterials()` in `mm/2s2h/BenGui/BenMenu.cpp` |

**Sources.** For every texture in the recipes the generator reads `alt/<resource>` from the highest-priority archive that
has it (the resource manager's own last-wins order), skipping its own output and any hand-built Python recolour archive.
So it follows whatever HD pack the player actually loads (MM Reloaded 4K today). Textures a pack lacks are skipped and
counted; with no pack at all the run takes 0.0 s, writes a `nosource` stamp and later launches do nothing.

**Jobs.** The 2,209 material textures (classification entries not excluded, then manifest-only entries, recipe =
defaults, manifest defaults, class, per-texture overrides) and the 42 recolour textures form one list sorted by size,
claimed by N worker threads (all cores at boot, half for the background button). A worker reads the texture, recolours it
if listed (the recoloured pixels feed the maps, as `materials.py --overlay-pack` did), fingerprints the winning albedo
pixels (FNV-1a, what the renderer checks), halves it 2x2, builds normal and roughness/relief, deflates both entries
(zlib level 3) in its own thread and appends them to the archive under a mutex. A small zip writer replaces libzip so
nothing but the current entries is held in RAM. The metadata JSON keeps only what the renderer reads (resource,
fingerprint, sizes, scales, type, flags).

**Cache.** `<app dir>/cache/newdawn-materials/live/{newdawn-recolor.o2r, newdawn-materials.o2r, stamp.json}`, always
written to `staging/` first. The stamp holds a signature: recipe version string, hash of the three data files, and the
name, size and mtime of every other loaded archive (so adding, removing or updating an HD pack or the ROM archive
regenerates; `2ship.o2r` is covered by the data hash). Regeneration happens when the signature or recipe version
changes, the files are missing, or the staging stamp from a button press is newer. An interrupted run leaves `staging/`
without a stamp, which the next launch deletes. A failed run is stamped too (no retry loop until something changes or the
button is pressed). Nothing here is under `mm/assets/newdawn`, which stays committed content only.

**Loading and ordering.** `AddArchive` the recolour archive, then the materials archive, after every mod. The resource
manager is last-wins, so the recolour overrides the pack's `alt/` albedos for its 42 textures, and the companions (new
`_n`/`_r` entries, plus `newdawn-materials.json`) sit on top. They are not in `mods/`, not in `gSettings.EnabledMods`, not
in the Mod Menu. The old `zy-`/`zz-` file names existed only because the Mod Menu mounts enabled mods in sorted
filename order and the recolour had to follow the pack and precede the maps; explicit mount order replaces that. A
player's own HD pack still works as before: whichever archive wins the albedo is what the maps are built from and what the
renderer's fingerprint check compares, so a pack that overrides a texture simply gets maps for its version (or none if
unlisted).

**AlternateAssets.** Before this change the archives were ordinary mods, mounted regardless of
`gEnhancements.Mods.AlternateAssets`, but they only had an effect with it on: `ResourceManager` looks up `alt/`
albedos only when the toggle is on, and `Interpreter::BindMaterialMaps` binds companions only to albedos whose path starts
with `alt/scenes/`. That is unchanged and correct, since the maps describe the HD textures. They are now mounted
unconditionally and independently of the toggle; with it off they stay mounted but unused (checked: 0 materials bind;
22 with it on at Termina Field), and the toggle remains the player's choice for community mods.

**Developer switch.** `gNewDawn.Materials.Generate=0` skips everything (status `Disabled`), e.g. to run with the Python
archives as plain mods for an A/B test.

## Measurements (RX 7800 XT box, 20 threads, local NVMe, MM Reloaded 4K v11.0.3)

| | Result |
| --- | --- |
| Generation, 2,209 maps + 42 recolours, 16 workers (boot) | **8.5-9.6 s** (4 runs); 10.6 s with 10 workers for the background button |
| Output | `newdawn-materials.o2r` 839.9 MB (Python: 842.1 MB), `newdawn-recolor.o2r` 131.7 MB (same as Python) |
| RAM | process RSS 1.3 GB with a warm cache; peak 2.4 GB sampled at boot generation (+1.1 GB), 2.6 GB during the button's background run; no archive held in memory |
| Warm launch | no popup, the stamp check is a few stat calls |
| No HD pack | 0.0 s, nothing written but a stamp |

**Match with the Python archives** (`tools/newdawn/compare_maps.py`, whole archives): all 2,209 metadata records agree on
every field the renderer checks (including the pixel fingerprints). Of 4,418 map entries **4,417 are byte-identical**; the
last differs in a single byte by 1 level (relief channel of `Z2_TOWNTex_01ADD0_r`, a percentile rounding tie). Per map type:
normal X max error 0, normal Y 0, roughness 0, relief 1 (one value in about 2.7 GB). All 42 recolour entries are
byte-identical. The same holds when the Python archives are also mounted as mods (`--mods sets/full`).

**Renders** (`~/dev/ghm-mods/showcase/runtime-materials/`, fixture: clock 12, frame 200, Ultra preset, identity cvars from
the agent rules; reference = the same binary with `gNewDawn.Materials.Generate=0` and the Python archives as mods):
Termina Field, South Clock Town (rot 320, the POM paving) and the Stock Pot Inn. 22 and 9 materials became ready in the
Termina Field and Clock Town reference and generated runs alike (`NewDawn material ready` log lines; none in the Inn).
Mean absolute difference reference vs generated equals reference vs reference run-to-run noise (Termina Field 0.41 vs 0.95,
Clock Town 0.8956 vs 0.8990, paving crop 0.182 vs 0.200 on a 0-255 scale); `crop_sct_pom_1to1.png` is the 1:1 paving crop.
Outdoor and Inn frames are not frame-exact repeatable on this fixture even against themselves, so the byte-identical
archives are the exact proof and the renders are the end-to-end check. 0 GL errors (`MESA_DEBUG=context`) in every run.

## Python tools

Kept for development, not needed by players: `materials.py` (reference implementation, `--classify` to rebuild
`material-classification.json` from a new pack, `--self-check`), `recolor.py` (reference and `--preview`),
`compare_maps.py` (regression check of the C++ port). Their default data paths now point at
`mm/assets/custom/newdawn_materials/`. After changing an algorithm in either implementation, rebuild the Python archives and
run `compare_maps.py` against a fresh in-game cache. `capture.py` keeps one persistent cache per mod set under
`<bin-dir>/captures/.materials-cache-*` (linked into each run's private `cache/`), so only the first capture of a set pays
the ~9 s; `--mods` still passes community mods. Use `~/dev/ghm-mods/sets/community` (packs only) for new work; the hand-built
`zy-`/`zz-` archives in `sets/full` are now redundant and harmless (ignored as a source, overridden by identical output).

## Limitations

- Needs an HD pack that provides the `alt/scenes/...` textures; without one there is nothing to build (the maps only bind to
  HD albedos, same as before). Maps for the native 32x32-class textures would need a renderer change and are out of scope.
- The classification and recolour recipes were calibrated against MM Reloaded 4K v11.0.3. A different pack gets the
  recolour applied to whichever texture wins at those paths; textures absent from the classification get no maps.
- Odd-sized or non-expanded-RGBA sources are skipped (counted as unusable). Output zips stay under 4 GB (no zip64); the
  writer fails clearly otherwise.
- Linux/OpenGL was run end to end. The code is portable C++ (`std::filesystem`, zlib, `std::thread`); the Windows/macOS
  builds are untested, and `find_package(ZLIB)` is a new build dependency (already part of the vcpkg set).
- The renderer re-parses `newdawn-materials.json` for every material it binds (existing behaviour; the file is now 0.54 MB instead of
  2.2 MB). Caching the parsed document per archive would be a cheap follow-up.
- Changing mods takes effect on restart, as before; the button does not hot-swap archives (the archive manager is not safe to
  mutate while worker threads read it).
