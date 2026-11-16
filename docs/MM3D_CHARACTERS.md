# 3DS character models

`gNewDawn.MM3D.Characters` (default 1; Graphics, Advanced, Experimental, "3DS character models (requires your MM3D
dump)"). Needs the player's own decrypted *Majora's Mask 3D* RomFS (`gNewDawn.MM3D.RomFS`). Without it the switch is
disabled and the game is exactly as before. ND-015; direction in ND-013 and ND-014; the feasibility spike is ND-013's "Next".

Visual only: the N64 skeleton, its limb offsets, animations, collision, hitboxes and actor code are read and never
changed. The 3DS meshes replace only the *display lists* of the limbs, so the original animation data drives them.

## How the player turns it on
1. Dump your own *Majora's Mask 3D* (decrypted RomFS, the folder that contains `actors/`, `scenes/`, `kankyo/`...).
   Nothing from it is shipped, committed or copied into the install; the same posture as the 2 Ship ROM extraction.
2. Graphics, Advanced, Experimental, **3DS Character Models**: "Choose RomFS Folder" (or type the path, or set
   `gNewDawn.MM3D.RomFS` in `2ship2harkinian.json`).
3. The next launch converts the models once behind a themed progress popup (seconds; all cores), caches them under
   `<app dir>/cache/newdawn-mm3d/live/mm3d-characters.o2r` and mounts the cache. "Convert 3DS Characters" redoes it in
   the background when the dump changes; the result is used on the next launch. The checkbox switches the models
   live; it is greyed out until a cache is mounted.

The cache is generated, never committed or redistributed (ND-008). It is keyed by a signature of the converter version,
the skeleton table, the colour settings, `mm.o2r` and the size and mtime of the dump's actor files.

## Architecture
```
 3DS RomFS (actors/zelda2_*.gar.lzs)           mm.o2r (N64 skeleton + limb resources)
          |  Mm3dFormats: LzS, GAR, CMB, ETC1/PICA textures        |
          +---------------------- Mm3dCharacters ---------------------+
                match bones to limbs -> bake into limb space -> limb display lists, vertex arrays, textures
                                             |
                    <cache>/mm3d-characters.o2r   entries named  mm3d/objects/<object>/...
                                             |
        ResourceManager overlay prefix "mm3d/" (libultraship): tried before alt/ when gNewDawn.MM3D.Characters is on
```
- **Overlay prefix** (`libultraship`, `ResourceManager::SetOverlayEnabled`): like the alt-asset lookup, but a separate
  switch. Resources are looked up as `mm3d/<path>` first; the cache stores `mm3d/objects/<object>/<limb>DL`, the vertex
  arrays and the textures under that prefix. It does not depend on `gEnhancements.Mods.AlternateAssets`, and the
  alt-asset mods (HD packs, 3DS Link) keep working untouched. Toggling clears the texture cache and re-patches skeletons
  like the alt-asset switch.
- **Matching** (`tools/newdawn/mm3d/chartable.py` generates `Mm3dCharacterTable.inc`, names only): an N64 Flex
  skeleton is matched to a CMB of the same-named 3DS actor archive whose rooted limb hierarchy is identical (children
  paired by subtree shape, then nearest joint offset). 58 skeletons match. Joint offsets equal the N64's exactly for
  most (the guard), and differ for some (Anju's legs, up to 138 units).
- **Binding**: every vertex goes into the local space of its dominant limb (`M_bone^-1 * v`, the 3DS bone frame is the
  N64 limb frame). Where the 3DS joint offset differs from the N64's, the child's mesh is shifted by the difference.
- **Flex-weld**: the N64's Flex skeletons expose every limb matrix as segment 0xD
  (`SkelAnime_DrawFlexOpa`, `mm/src/code/z_skelanime.c`). Each vertex is loaded under its own dominant limb's matrix
  (`gSPMatrix(0x0D000000 + slot * 0x40 | 1)`), the owner's matrix is restored before the triangles, so shared
  vertices never separate (no cracks) and triangles that span a joint stretch, as the original N64 meshes do. No
  renderer skinning is needed. Slots follow the draw order of limbs that have a display list.
- **Textures**: CMB textures (ETC1, ETC1A4, RGB565...) are decoded to RGBA32 resources flagged load-as-image; the
  display list declares at most 64x64 (the LOADBLOCK limit) and scales the UVs to that, the resource keeps its size.
- **Table** (`Mm3dCharacterTable.inc`): object, skeleton, archive, CMB entry, props to leave out (material indices) and
  `reviewed`. Only reviewed models are written to the cache and enabled; the others are converted and counted but held
  back until someone has looked at them in game (props, slot order, faces). `chartable.py` keeps the curated sets.

## Colour
The 3DS combiner multiplies the texture by the fragment-lighting term
`ambient * 0.4 + diffuse * 0.5 * N.L` (material constants 102 and 127 on nearly every character); the N64 display lists
multiply by the actor's full light. The same texture therefore came out about half as bright and flat. The material
constants are folded into the texture when it is converted: `gain = LightRef / (0.6 * diffuse + 0.4 * ambient)`, then
a saturation lift. `gNewDawn.MM3D.LightRef` (0.6) and `gNewDawn.MM3D.Saturation` (1.35) are tunables and part of the
signature. Specular sphere maps (`sp_*` textures) and other second texture units are not mapped.

Art direction (maintainer, 2026-11-16): characters are colourful and tactile against a darker world, so the defaults
lean saturated rather than faithful to the 3DS palette. The study images (`~/dev/ghm-mods/showcase/mm3d-characters/
colour_*_study.png`) compare LightRef 0.5 to 0.8 and saturation 1.1 to 1.55 on the guard, Anju and the receptionist:
1.35 keeps skin and hair vivid without clipping; 1.55 turns the guard's skin and Anju's hair garish.

## Faces
The 3DS eyes and mouths are separate planes with one 32x32 texture each, animated by the actor's material animations
(`*.cmab`, texture patterns and UV offsets). The N64 draws its eyes from segment 0x08/0x09 textures chosen by the actor.
Converted models keep the 3DS eye and mouth planes static (open eyes, neutral mouth); the actor's blink and talk textures
are not mapped. Mapping them needs the CMAB decoded and a per-actor table from N64 eye states to 3DS frames (milestone 2).

## Status (milestone 1)
Implemented, built (RelWithDebInfo, `nd-heavy build`) and run in game.

| | |
|---|---|
| Skeletons in the table | 58 Flex skeletons with a hierarchy identical to a CMB of the same-named 3DS archive (`chartable.py`) |
| Converted / failed | **58 / 0** (every CMB parses, binds and writes) |
| Conversion time | **0.03 s** for all 58 (20 threads, includes reading the GARs and the N64 skeletons; 51,078 triangles, 154 textures, 9,114 triangles span a joint) |
| Cache | 173 KB for the 4 enabled models |
| Enabled in game (reviewed) | **4**: Clock Town guard, Anju, Mayor's Residence receptionist, Business Scrub |
| Held back (converted, not enabled) | 54, until each has been looked at in game |
| Off identity | toggle off with the cache mounted, and no dump at all: frozen Stock Pot Inn (clock 12, frame 30, the extended off list) is **pixel-identical** (0 differing pixels) to main `85fbd4fd4`, which itself reproduces exactly run to run |
| GL errors | 0 in every capture (`MESA_DEBUG=context`) |

Images: `~/dev/ghm-mods/showcase/mm3d-characters/` (`*_n64_vs_3ds.png`, bursts, colour studies).

### Validation set
The brief asked for the guard, Anju, the Postman, Kafei, the Mayor, Romani or Cremia, the Bombers and two or three
enemies. What could be verified:

| Character | Result |
|---|---|
| Clock Town guard (`object_sdn`) | in game, idle crouch, same frame as N64; flex-weld closes the shoulder and hip cracks the rigid spike showed |
| Anju (`object_an1`) | in game at the Stock Pot Inn counter; face, hair, vest and hands |
| Receptionist (`object_bg`, Mayor's Residence) | in game; face and blouse; her N64 actor draws with `SkelAnime_DrawTransformFlexOpa` and the flex matrices still line up |
| Business Scrub (`object_dnt`) | in game in the Southern Swamp (the two frames caught different actor states, so only the model is comparable) |
| Postman (`object_mm`), Mayor Dotour (`object_dt`) | convert fine; **no in-game capture**: the Postman was not where the scene's actor list put him in the three rooms tried, and the Mayor is not in his residence at 10:00 on day 1 |
| Takkuri, Wolfos, Dinolfos, Big Octo, dog | convert fine; **no usable in-game capture** (a flying Takkuri and the Snowhead Wolfos never entered the frame; the Octo renders translucent in poison water and was held back). Needs an actor-aware capture fixture (aim at the live actor), milestone 2 |
| Kafei, Romani, Cremia, Bombers | **no match**: their 3DS rigs differ from the N64 ones (Romani 22 vs 25 bones with different offsets, Cremia 21 vs 23, the Bombers 20 vs 17, no Kafei CMB found by name). They need the leaf and contraction reduction plus per-limb registration (milestone 2) |

Offline joint checks (software render of the same N64 animations, `ref/measure.py` from the spike; flex-weld has no seam by
construction): guard (idle, wave, come-up-here), Anju (walk, idle, bow), receptionist (her idle) and the Business Scrub
(excited, jump, bob). Flex displacement from true weighted skinning: mean 5 to 17 units, p95 41 to 112; triangle stretch p99
1.95 to 3.4x, worst 7.5x (scrub, excited). Rigid-per-limb seam gaps for comparison: mean 94 to 238 units.

### Limits
- Eye and mouth textures are static (see Faces). Props baked into a CMB are listed per skeleton (guard bottle, Anju's
  moon mask, the bomb shopkeeper's bomb); other models were not scanned for props beyond the validation set.
- Flex-weld assumes the actor draws every limb through the Flex matrix pool in table order. An actor that skips limbs
  in its override callback shifts the slots; that is what the review in game is for.
- PICA combiner effects beyond texture times fragment light (sphere-map shine, second texture units, vertex colours,
  texture animations) are ignored.
- The live toggle (`ApplyToggle`) is the alt-asset switch's mechanism; it was exercised at boot, not by clicking the
  checkbox in the menu.

### Milestone 2
1. Remaining skeletons: review the 54 held-back models (props, faces, slot order) with an actor-aware fixture, enable
   them in `chartable.py`; add the leaf-bone and contraction reduction (17 skeletons) and per-limb registration for
   rigs with different proportions (Romani, Cremia, the Bombers, Kafei once its CMB is found).
2. Faces: decode the CMAB texture animations and map N64 eye and mouth states to 3DS frames per actor.
3. Link's five forms: the Player draw path, 21 limbs against 24 to 28 bones, equipment, hands and masks; the 3DS Link mod
   keeps working through alt assets meanwhile.
4. Optional interpreter skinning only if stretch at joints proves visible on the enabled set.
