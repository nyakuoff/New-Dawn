# 3DS character models

`gNewDawn.MM3D.Characters` (default 1; Graphics, Advanced, Experimental, "3DS character models (requires your MM3D
dump)"). Needs the player's own decrypted *Majora's Mask 3D* RomFS (`gNewDawn.MM3D.RomFS`). Without it the switch is
disabled and the game is exactly as before. ND-014; direction in ND-013; the feasibility spike is ND-013's "Next".

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
a saturation lift. `gNewDawn.MM3D.LightRef` (0.55) and `gNewDawn.MM3D.Saturation` (1.1) are tunables and part of the
signature. Specular sphere maps (`sp_*` textures) and other second texture units are not mapped.

## Faces
The 3DS eyes and mouths are separate planes with one 32x32 texture each, animated by the actor's material animations
(`*.cmab`, texture patterns and UV offsets). The N64 draws its eyes from segment 0x08/0x09 textures chosen by the actor.
Converted models keep the 3DS eye and mouth planes static (open eyes, neutral mouth); the actor's blink and talk textures
are not mapped. Mapping them needs the CMAB decoded and a per-actor table from N64 eye states to 3DS frames (milestone 2).

## Status (milestone 1)
See the end of this file for the conversion counts, the validation set and the known limits.
