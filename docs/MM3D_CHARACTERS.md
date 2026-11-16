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
a saturation lift. `gNewDawn.MM3D.LightRef` (0.55) and `gNewDawn.MM3D.Saturation` (1.15); a model whose palette is already strong carries its own values in the table (`COLOUR` in `chartable.py`: the guard 0.5 / 1.0, the receptionist 0.55 / 1.05) are tunables and part of the
signature. Specular sphere maps (`sp_*` textures) and other second texture units are not mapped.

Art direction (maintainer, 2026-11-16): characters are colourful and tactile against a darker world, so the defaults
lean saturated rather than faithful to the 3DS palette. The study images (`~/dev/ghm-mods/showcase/mm3d-characters/
colour_*_study.png`) compare LightRef 0.5 to 0.8 and saturation 1.1 to 1.55 on the guard, Anju and the receptionist:
1.35 looked vivid on Anju but made the guard's skin and trim and the receptionist's face garish (maintainer review, 2026-11-16), so the default is 1.15 with per-model overrides; 1.55 is garish everywhere.

## Textures
The 3DS character textures are 128x128 atlases (faces and hair 32x32), against 1024x1024 in the community pack for the
same N64 characters. Four ways to close the gap were compared 1:1 in game on the guard, Anju and the receptionist
(`~/dev/ghm-mods/showcase/mm3d-characters/textures/`, one sheet per option and character):

| Option | Result | Verdict |
|---|---|---|
| **A. 4x AI upscale at conversion time**, PBRify SPAN V4 (CC0 weights) | Mail reads as rings instead of zebra stripes, ETC1 dithering and block edges disappear, hair and faces stay clean; the 3DS design and colours are untouched | **Shipped.** CPU port, 0.76 s per 128 px texture on one core, about 1 s for the four enabled models on 20 threads |
| A2. Real-ESRGAN anime x4plus (BSD-3 code) | Crisper face line art and hair strands in the atlas, but it flattens the guard's mail into horizontal bars, so in game the guard is worse than with SPAN; training data not stated (anime images from the web) | Not shipped: worse on the model players notice most, and its dataset licence is unclear |
| B. Transfer of the N64 model's HD texture onto the 3DS mesh in the shared bind pose | Full transfer imports the N64 colours and the wrong content wherever the two designs differ: tiger stripes on the guard's thighs, a grey spear, hair strands on Anju. A high-pass-only transfer (3DS design kept, HD detail added) looks like A plus artefacts | **Not adopted.** Needs a per-region reliability mask (faces, skin and shapes that differ) that does not exist yet; also needs the N64 display lists and the pack at conversion time |
| C. Normal and roughness maps for the upscaled textures (`tools/newdawn/materials.py` recipe) | Helmet and mail gain a metallic response; skin and faces go flat, greenish and patchy, and the whole character is darker (the material shading has no matching gain) | **Not adopted** as generated. Needs a class mask (skin, hair, cloth, metal) and tone compensation |

The renderer binds generated maps to textures under `alt/scenes/` only. Option C was tested with a temporary change
that also accepted `mm3d/` albedos (maps stored next to the albedo, nothing to verify); the change is not part of this
branch, so enabling C later is a small `BindMaterialMaps` edit plus the class mask.

Implementation: `SpanUpscale.cpp` is a direct CPU port of the network (22 convolutions, 426k parameters; weights in
`mm/assets/newdawn/mm3d/span_v4_4x.bin`, 1.7 MB, CC0, `export_span.py` makes it from the ONNX export). It matches ONNX
Runtime 1.32 within 1/255 on a real texture. Textures are edge-replicated (they are atlases, not tiles) and alpha is
resized bilinearly; the upscale runs before the colour mapping. The display list keeps declaring the original size, so
the UVs still fit. `gNewDawn.MM3D.Upscale=0` (menu checkbox) skips it; weights missing or the switch off leave the 3DS
size. If all 58 models were enabled the upscale would be about 190 textures, 60 s of single-core CPU, so roughly 5 s on
20 threads and 30 s on two cores; the result is cached with the models.

## Faces
The 3DS eyes and mouths are separate planes with one 32x32 texture each, animated by the actor's material animations
(`*.cmab`: a table of texture frames, the animation frame picks the texture). The N64 draws its eyes and mouth from
segment 8 and 9, which the actor points at one of its state textures (open, half, closed, happy, ...) each frame.
- The converted head display list reads the same segment for the eye and mouth materials (`G_SETTIMG` with the segment
  address, bit 0 set for the interpreter), so the actor's own blink and talk logic drives the face.
- The overlay holds a 3DS frame under each N64 state texture's path (`mm3d/objects/<object>/<state texture>`). Which frame
  stands for which state is the `FACE` table in `chartable.py` (`E`/`M`, segment, CMAB, `state=frame`), matched by eye on
  montages of the N64 textures and the CMAB frames (`ParseCmabFrames` reads them; they are RGB565 or ETC1).
- A face part is applied only when frame 0 of the CMAB matches one of the model's textures (mean RGB difference below 40);
  otherwise the face keeps its first frame as before. Frames go through the same upscale and colour mapping as the rest.
- The segment-addressed texture path had to learn about the overlay: with alternate assets on, the interpreter's async path
  went straight to `alt/` and drew the HD pack's N64 face textures on the 3DS faces (libultraship, `ResolveTextureResource`).
- Checked in game (same frame N64 vs 3DS, `showcase/mm3d-characters/review/`): Anju blinks (open, half-lid, closed) and
  her mouth changes (smile, parted lips) with the N64 actor's states; the treasure gal's open eyes and lips; Romani, Cremia and
  a Bomber draw their faces without artefacts but too small to judge states; the tree climber faces away; Kafei, the receptionist
  and the other wired states were not framed. No frame of a talking mouth in Anju's N64 conversation opened wider than parted lips.
- States wired: Anju (6 eye, 3 mouth), the tree climber, receptionist, treasure gal, Romani, Cremia, a Bomber (eyes), Kafei.
  Others keep the first frame. Not mapped: the Bombers' number textures (segment 9 on the shirt), the Wolfos and other
  models with a CMAB but no per-state table yet, UV-offset animations.

## Status (milestone 1)
Implemented, built (RelWithDebInfo, `nd-heavy build`) and run in game.

| | |
|---|---|
| Skeletons in the table | 53: 49 with a hierarchy identical to a CMB of the same-named 3DS archive (58 matched, 9 excluded) and 4 paired by a rig recipe (`chartable.py`) |
| Converted / failed | **53 / 0** (every CMB parses, binds and writes) |
| Conversion time | **0.03 s** for all 58 without the texture upscale; **1.0 s** with it for the 4 enabled models (20 threads, includes reading the GARs and the N64 skeletons; 51,078 triangles, 154 textures, 9,114 triangles span a joint) |
| Cache | 2.4 MB for the 4 enabled models with the 512 px textures (173 KB before the upscale) |
| Enabled in game (reviewed) | **6**: Clock Town guard, Anju, Mayor's Residence receptionist, Business Scrub, Treasure Chest Shop gal, the tree climber in Termina Field |
| Unreviewed (converted, off unless "Include unreviewed 3DS models (experimental)" is on) | 47, see "Review of the held-back models" |
| Excluded (not converted at all) | 9 skeletons, `EXCLUDE` in `chartable.py` |
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
| Treasure Chest Shop gal (`object_bg`, `gTreasureChestShopGalSkel`) | in game; purple hair and pink top kept. The archive holds two CMBs with this hierarchy (`reception_girl`, `treasure_girl`); the table now picks per skeleton (`CMB` in `chartable.py`), which a first capture showed was needed (she came out with the receptionist's green hair) |
| Tree climber (`object_ani`, Termina Field) | in game, same frame as N64; body, hair and clothes line up |
| Takkuri, Wolfos, Dinolfos, dog | convert fine; no usable in-game view (the dog was hidden behind Link, the flying Takkuri and the Snowhead Wolfos stayed out of frame) |
| Big Octo | **rejected in game**: the 3DS mesh draws translucent and a different shape in the poison-water room |
| Kafei, Romani, Cremia, Bombers | **no match**: their 3DS rigs differ from the N64 ones (Romani 22 vs 25 bones with different offsets, Cremia 21 vs 23, the Bombers 20 vs 17, no Kafei CMB found by name). They need the leaf and contraction reduction plus per-limb registration (milestone 2) |

Offline joint checks (software render of the same N64 animations, `ref/measure.py` from the spike; flex-weld has no seam by
construction): guard (idle, wave, come-up-here), Anju (walk, idle, bow), receptionist (her idle) and the Business Scrub
(excited, jump, bob). Flex displacement from true weighted skinning: mean 5 to 17 units, p95 41 to 112; triangle stretch p99
1.95 to 3.4x, worst 7.5x (scrub, excited). Rigid-per-limb seam gaps for comparison: mean 94 to 238 units.

### Review of the held-back models (milestone 2)
The Graphics menu option "Include unreviewed 3DS models (experimental)" (`gNewDawn.MM3D.ReviewAll`, default off, applied on
the next launch or Convert) also writes the 47 models that have not been checked in game, so they can be reviewed while
playing; the default stays the 6 `reviewed` rows of the table. Checked at boot: off 6 enabled, on 53, off again 6. A scan of 25 scenes with it on found live actors for 13 of
the 50 objects (guard, Anju, receptionist/treasure gal, scrub, dog, tree climber, Postman, tied monkey, Big Octo, Real
Bombchu, Takkuri, Snowhead Wolfos, balloon Poe); the other 37 objects only appear in cutscenes, at other times of day or in
rooms the fixture did not reach. The bind-pose gallery of all 58 (flat colour, front and side) gave these offline verdicts:
- Same CMB for a variant that looks different: both Pamela's father skeletons use one human CMB, so the Gibdo form would
  turn into a human; the Great Fairy has five colour CMBs for one skeleton; White Wolfos and the Garo pair needed their
  own CMB (now picked). 
- Not a plausible stand-in: Deku Princess (a flower on a stalk), Biodeku lily pad (a flat red hexagon), Big Octo.
- Props baked into the CMB, not yet excluded: Iron Knuckle axe, the witch sisters' brooms, Wizzrobe staff, the pirate spear.
- The Postman (`En_Pm`, post office) rendered identically in both modes; the cause was not determined, so `object_mm` stays off.
Everything else is plausible offline but unseen in game, so it is only in the experimental option.

Excluded from the table (never converted, even with the option on): Big Octo, Pamela's father as a Gibdo, Deku Princess,
Biodeku lily pad, Postman, Iron Knuckle and Takkuri (both keep `*dList` of a limb and draw it later as flying armour or
death pieces, outside the Flex matrix pool the flex-weld display lists read), the pirate (his weapons were not compared
with the N64 swords) and the Great Fairy (one skeleton, five colour models; the model cannot follow her param).
Props: the witches' brooms, the Wizzrobe staff, the pirate weapons and the Iron Knuckle axe are limbs of the N64 skeleton
too (`KOUME_LIMB_BROOM`, `WIZROBE_LIMB_STAFF`, `IRON_KNUCKLE_LIMB_AXE`), so the matched bones drive them and nothing
doubles; the only separately drawn prop found is Koume's potion bottle (`gKoumeBottleDL`), now in her `SKIP` list.

### Rig reduction (milestone 2)
Kafei, Romani, Cremia and the Bombers do not match an N64 hierarchy: the 3DS rigs have clavicles, split spines, a skirt of
three bones, or a wrist and hand where the N64 has a pivot and two parts at the same joint. Their rows carry a rig recipe
(`RIG` in `chartable.py`, "bone:limb" pairs, found from both trees and the joint offsets):
- listed bones drive the paired limb; an unlisted 3DS bone follows its nearest listed ancestor (clavicles go to the torso,
  the head's second bone to the first, the skirt chain to its first bone, the right wrist's pivot to the hand);
- an unlisted N64 limb gets an empty display list (Dress2 to 4, the Bombers' spare head parts);
- the rest shift of a bone is the N64 offset to its nearest listed ancestor limb (the unlisted pivots in between added up,
  their rest rotation taken as identity) against the offset of the bone from the 3DS bone paired with that limb, both from
  the world bind matrices.
Stature: a rigged model is scaled uniformly about the origin so its root bone sits at the N64 root limb's rest height
(Kafei 1.10, Romani 0.95, Cremia 1.02, Bombers 1.0); without it the feet float or sink by up to 220 units, since the N64
animation places the root, not the feet.
Pairing the skirt bones with the N64 dress limbs flared Cremia's skirt: the 3DS skirt bones carry rest rotations (up to
0.77 rad) the N64 dress limbs do not, so those chains were folded into one bone. Everything in a recipe must share rest
rotations with the limb it is paired with; a chain that does not has to be folded.

In game, same-frame N64 vs 3DS (`showcase/mm3d-characters/review/`): **Romani** (walking, Romani Ranch), **Cremia**
(milking, same scene) and a **Bomber** (East Clock Town) pose like the N64 model. **Kafei** converts (0 failures) but was
not framed: the one scan scene with him (South Clock Town) is the intro cutscene, so he is unverified in game. All four stay
in the experimental option. The five Bombers share one 3DS model (one hat), so with the option on they look alike, where the
N64 gives each their own hat colour.

### Link, human form (milestone 2)
`object_link_child/gLinkHumanSkel` against `zelda2_link_child_new/child/model/link_child.cmb` (26 bones, 95 draws, 42 materials).
It is a row with a rig recipe (`RIG`), the same recipe as Kafei's: the 3DS and N64 limbs share one layout, the torso hangs
off the upper-root pivot (`9:9>20`: the bone's frame is the pivot's, its mesh is drawn under the torso limb). In the table the
row sits behind "Include unreviewed 3DS models" until masks and the other forms are checked.
- **What is converted:** body, tunic, hat, head with hair, face and eyes. **What stays N64:** the hand display lists (limbs 15
  and 18, which the Player override swaps per held item: sword, shield, bow, ocarina, hookshot, bottle) and the sheath
  (limb 19); the 3DS CMB carries the equipment too (draws 13-34 and 44-94: ~20 hand poses, bow, shield, sword models), which
  is left out for now with `d<n>` entries in the row's skip list. Every hold point therefore stays the N64's: the sword, shield
  and bow follow the hand limbs exactly as before.
- **Hat:** the 3DS hat is 2636 units long and 1195 wide against the N64's 1702 and 901 (head 1.2x the N64's), so it read
  as oversized. `s5=0.66:0.85@12` shortens draw 5 to 0.66 along its axis and narrows it to 0.85 about the hat bone's origin
  (the base keeps covering the hair). Same-frame crops: `showcase/.../review/link_human_hat_green_before_after.png`.
- **Green:** the tunic and hat greens measured value 0.50 (Clock Town) and 0.57 (Termina Field) against the N64's 0.29 and 0.44,
  saturation 0.99 (r and b clipped to 0: neon). The row's tint, `1:-8:0.85:0.68:60:180`, shifts hue by -8 degrees, scales
  saturation by 0.85 and value by 0.68 on greens only (hue 60-180, the buckle and boots share the texture): now 0.32 and 0.46,
  hue 109 (N64 111). The hair gets its own tint (`11:20:0.8`, ginger towards the N64's blond).
- **Faces:** the 3DS eye and mouth frames follow the N64 states (`FACE`); checked that eyes close (blink) and open in a run.
- **Checked in game** (same scripted input, N64 vs 3DS, 0 GL errors): run, run to a halt, sword swing, shield up, bow aim
  (first-person view: the bow is the N64 bow display list in the right hand, identical in both), stand at idle.
  Not checked: mask attachments (an `equippedMask` poke did not take effect), the other forms, the hookshot, bottle and ocarina,
  swimming, climbing, and cutscene poses.
- **Head direction (review of the first run sheet):** the head-down 3DS frames of the first sheet were a comparison of different
  animation phases (the two builds' bursts drift by a few frames, and Link bows his head in the swing recovery in the N64 too),
  not a rig fault. Checked three ways with the final build: (1) a long same-input burst in both builds (roll, three swings,
  shield, side hop, back flip, spin; `link_human_burst_n64.png` / `_3ds.png`) and a phase-matched sheet
  (`link_human_run_sword_n64_vs_3ds.png`): upright head wherever the N64's is, bowed where it bows; (2) an offline overlay
  that poses both the N64 and the converted 3DS meshes with the same N64 animation matrices over 12 animations (sword
  kiru, power-jump kiru, rolling kiru/spin, landing roll, shield, bow ready, back-turn jump, side jump, turn kiru, run, walk,
  bow defence), 4 frames each (`link_human_head_overlay_12_animations.png`): the 3DS head and hat silhouette covers the
  N64's in every frame, so the head follows the head limb's matrix and nothing else; (3) a rigid fit of the 3DS head mesh to
  the N64's in the limb frame: 6.8 degrees about the lateral axis, left as is (the heads differ in shape, so the fit
  absorbs some of it; the hat, shoulders and forearms fit within 7 to 37 degrees, the largest from the 3DS arms' different
  rest bend). The same fit would show a missing neck bone or a double rotation as a large angle on the head.
- Masks: poking `equippedMask` in the save and `currentMask` on the live Player actor (both with the form fixture) draws no
  mask in either build; in-game mask changes are unverified for all five forms.
- Rig quirks found: the hat bone's rest rotation (2.16 rad) differs from the N64 hat limb's, so the hat hangs steeper than the
  N64's; per-limb rotation registration over several animation frames did not give a clearly better frame for any limb.

### Link, Deku form
`object_link_nuts/gLinkDekuSkel` against `zelda2_link_nuts_new/nuts/model/link_deknuts.cmb` (24 bones, 25 draws): the same 21-limb
layout and the same rig recipe as human Link. Skipped: the hand and fist meshes (the N64 hand display lists are kept, limbs
15 and 18, as for every hold point), the bottle and cork, a stray triangle and the 3DS Deku shell (`link_nuts_shiel`, a hood
that sits around the head). The hat is 0.75 of its 3DS size (the N64 hat measures 3339 x 1859 x 1519, the 3DS 4495 x 2454 x 2043).
- **Eyes:** the 3DS eye planes sit inside the face mesh (the face is an opaque shell). Draw 22 is pushed 300 units along its
  normals (`o22=300`) and material 5 is drawn unlit (`u5`: the PICA material has no fragment lighting, so its glow is the
  texture alone; without it the eyes came out brown). New skip tokens: `o<draw>=<units>` and `u<material>`.
- **Greens:** hat and tunic use the human Link tint with the hue window starting at 85 (the hair is yellow-olive, hue 60).
- Checked in game (`gNewDawn`'s private fixture sets `playerForm` in the save: a Deku at spawn, run toward the camera,
  same input in both builds): the pose, hat and limbs follow the N64 animation; the face is the 3DS one (wood face with a
  tube mouth, orange eyes) against the N64's dark face with a black mouth.
- Not checked: pipes, flower, bubble, spin, hopping on water, the Deku Link cutscene transformations.

### Link, Goron, Zora and Fierce Deity forms
The same recipe again (`1:0,2:1,3:6,4:7,5:8,6:3,7:4,8:5,9:9>20,11:10,12:11,15:13,16:14,17:15,19:16,20:17,21:18,22:19`: all five 3DS
Link rigs share the 21-limb layout; only the bone offsets differ and the rest shifts take care of that), per form a skip list
that leaves the hand, gauntlet and equipment meshes out and keeps the N64 hand display lists (limbs 15, 18, 19):
| Form | 3DS archive and model | Left out (draws) | Other |
|---|---|---|---|
| Goron | `link_goron_new` / `link_goron.cmb` | fists 10-14, cork 8, bottle 15 16 | eye states from the CMAB (open, half, closed, surprised); colour (0.5, 1.0) |
| Zora | `link_zora_new` / `link_zora.cmb` | fins 12-15 (the game draws its own), hand poses 18-29, barrier 30, cork and bottle | eye and mouth states in human Link's order |
| Fierce Deity | `link_boy_new` / `link_demon.cmb` | gauntlets 1-7, hand poses 15-19, the Double Helix 9, cork and bottle | static face (the N64 has one eye and one mouth texture) |
Checked in game with the form set in the save (private fixture): each form runs toward the camera in the same frames as the
N64 model and the pose follows. Not checked: the sword, shield, bow or anything held in these forms, transformation
cutscenes, the Goron roll and spikes, the Zora swim, boomerang fins and barrier, the Fierce Deity sword beam. The 3DS Goron
and Fierce Deity read differently from the N64 ones (the 3DS Goron has a blue necklace and white beard, the Fierce Deity
teal clothes and green hair).

### Limits
- Eye and mouth states exist only for the models listed under Faces. Props baked into a CMB are listed per skeleton (guard bottle, Anju's
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
