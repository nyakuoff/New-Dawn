# Replacing models and textures with really modern ones: research report

Research only. No code was changed and nothing was committed. Everything below was measured on this machine on
2026-11-15 against `nd-research` (main `9b5bccec9`), the local `mm.o2r`, the local MM Reloaded 4K v11.0.3 archive
and the user's own MM3D (3DS) dump. Scripts are in `scripts/`, the proof-of-concept images are in `poc/` (derived
from game data, so they stay outside the repository and are never committed). The comparison of strategies and my
recommendation are in `OPTIONS.md`.

How to read the evidence tags: **[measured]** = I ran it here; **[primary]** = I read the licence/spec text
from its own project page or repository; **[secondary]** = a news/summary page, not the licence text;
**[estimate]** = my reasoning, not verified.

---------------------------------------------------------------------------------------------------------------

## 0. Ten-line summary

1. The game has **13,542 textures**, but only **8,284** are world and character art (scenes 3,518, objects 4,766).
   After removing byte-identical duplicates that is about **6,450** distinct images. UI, fonts and kanji are
   another 5,258 and are mostly 16×16 glyphs. Together the world and character textures hold **9.3 Mpx**,
   barely more than one 4K frame.
2. There are **214 skeletons in 174 object folders**, median 17 limbs, max 51. **All five Link forms share the same
   21-limb Flex skeleton.** The whole game's object display lists hold about 118,000 triangle commands.
3. A model replacement works through the generic `alt/<path>` override, which applies to *any* resource, and the
   engine already has a "Flex" skeleton type (172 of 214 skeletons) where every limb's matrix is available to
   every limb's display list. This is why the 3DS Link mod works. The mod is still **rigid per limb**: no vertex
   weights, no blending.
4. N64 vertex-weighted skinning exists in the engine for **one skeleton only (Epona)**. Real linear-blend skinning
   for replacement models would be new renderer work (feasible, because the Flex matrix array already reaches the
   GPU; needs a prototype to confirm).
5. **The existing material system (ND-009) only binds to `alt/scenes/` HD albedos.** Today every material map in
   New Dawn depends on a community HD pack that is 24.65 GB and has no redistribution licence. Any texture
   strategy that produces its own `alt/scenes/` albedos revives the material system without that pack.
6. **Licence reality check [primary]:** the 3DS Link and HD Moon mods are CC BY-NC-ND 4.0, MM Reloaded's
   repositories carry no licence, and Hunyuan3D 2.1 excludes the EU, UK and South Korea. None of these can be
   shipped by New Dawn. ambientCG and Poly Haven are CC0 and can. PBRify (CC0 upscaling models trained only on
   ambientCG) and Real-ESRGAN (BSD-3) can be shipped as *tools*, but their output on game textures is
   game-derived and must be generated on the player's machine.
7. **Proof of concept [measured]:** a 4× AI upscale of a 32×32 original gives a clean 128 px image in ~12 ms per
   texture on a CPU. It is sharper than the original but not "modern": it invents detail (grass became leaf veins,
   wood became plaster). The community pack, hand-processed to 1024-2048 px, is far richer. A CC0 photo-scan library
   has the right detail but the wrong **scale and style** (tiny photographic bricks against the game's big chunky
   ones).
8. **MM3D models [measured]:** 3DS Link is 26 bones and 8,393 triangles (N64: 21 limbs, 2,176 across all of
   Link's display lists). For the whole cast, **65 of 214 N64 skeletons have an identical hierarchy** in the
   same-named MM3D archive and 17 more are "N64 plus extra leaf bones". All five Link forms need re-binding.
   MM3D character textures are tiny (Link's largest is 128×128), so MM3D gives better geometry, not better textures.
9. AI 3D generation is a poor fit: the usable models need 24 GB NVIDIA GPUs, the licences carry territory or
   non-commercial strings, the meshes have no relation to the N64 skeleton, and generating named Nintendo
   characters means feeding Nintendo-derived images in.
10. A separate legal point needs a maintainer decision: any model or texture pack *we* ship that depicts Nintendo's
    characters is a fan-made derivative of their IP, even when every pixel is ours. 2Ship avoids this by shipping
    code only. See section 8.

---------------------------------------------------------------------------------------------------------------

## 1. Scope and inventory (question A)

All figures are from `~/dev/2ship2harkinian/build-cmake/mm/mm.o2r` (36.3 MB, 50,496 entries), classified by the
4-byte resource tag in each entry header. Scripts: `scripts/types.py`, `breakdown.py`, `texdims.py`,
`skeletons.py`, `tris.py`, `actors.py`.

### 1.1 Resources

| Resource | Count | Note |
|---|---:|---|
| Textures (OTEX) | 13,542 | see 1.2 |
| Display lists (ODLT) | 15,430 | |
| Arrays (OARR, mostly vertex arrays) | 10,481 | |
| Skeletons (OSKL) | **214** | in 174 of 464 object folders |
| Skeleton limbs (OSLB) | 3,495 | |
| Animations (OANM) | 2,453 | 2,444 in objects; 713 sit in `gameplay_keep` (shared Link and system animations) |
| Collision headers (OCOL) | 290 | |
| Scenes | 102 in the scene table, 100 with textures | 22,379 resources under `scenes/` |
| Actor overlays | 573 with a profile (574 directories) | NPC 154, Prop 112, ItemAction 105, Bg 91, Enemy 72, Boss 19, Switch 7, Door 6, Misc 4, Explosives 2, Chest 1 |
| Object table | 647 entries | 464 object folders in the archive |

### 1.2 Textures: world vs character vs UI

| Group | Textures | Distinct (byte-identical removed) | Pixels | Share ≤ 1,024 px |
|---|---:|---:|---:|---:|
| World: `scenes/` | **3,518** | 2,480 | 5.1 Mpx | 68 % (2,409) |
| Characters, props, actors: `objects/` | **4,766** | 3,968 | 4.2 Mpx | 83 % (3,964) |
| UI: kanji, fonts, icons, maps, schedules | 4,941 | 4,594 | 2.5 Mpx | 4,165 are 16×16 |
| Other (`misc`, overlays, `code`) | 317 | 279 | 0.9 Mpx | |
| **Total** | **13,542** | 11,103 | 12.7 Mpx | |

Of the 4,766 object textures, 2,912 (2,508 distinct) sit in the 172 skeleton-bearing object folders (animated
characters and enemies), 1,446 (1,215 distinct) in 289 prop and set-piece folders, and 408 in the three
`gameplay_*keep` folders.

Formats of all textures: I4 4,917, RGBA16 3,684, CI8 2,522, I8 674, CI4 546, IA8 501, IA4 308, IA16 225,
RGBA32 165. Palette textures are 3,012 of the 8,284 world and character textures (36 %), which matters for any
local decode.

The takeaway is that **the whole replaceable surface is small in pixels**. The pack that already covers it
holds 700× more (see 1.5).

### 1.3 Models: skeletons, limbs, triangles

| Measure | Value |
|---|---|
| Skeletons | 214 in 174 object folders (172 outside `gameplay_*`: 205 skeletons) |
| Skeleton type | Flex 172, Normal 40, Curve 2 |
| Limb type (limb resources) | Standard 3,303, LOD 130, Skin 46 (all in the Epona object), Curve 16 |
| Limbs per skeleton | min 1, **median 17**, max 51 (Odolwa); most common: 15 (21 skeletons), 23, 21, 19 |
| Triangle commands in all object display lists | ~118,600 (upper bound, each display list counted once) |
| Link Human object (every DL: equipment, hands, masks) | 2,176; Goron 1,637; Zora 1,023; Deku 1,018; Fierce Deity 835 |
| Typical NPC | 300-700 (Anju 593, Soldier 445, Bomb Shop owner 540); big ones: Majora object 2,126, Igos/Knight 1,214 |

All five Link forms (`gLinkHumanSkel`, Goron, Zora, Deku, Fierce Deity) have **21 limbs** and use Flex. The
transformation masks and the animations they share (713 in `gameplay_keep`) therefore apply to a common rig.
Tatl is not a big model: the fairy uses `gameplay_keep_Skel_02AF58`, six limbs, drawn by `En_Elf`.

Skeleton-bearing object folders, grouped by what actor uses them (an object can serve several categories, so the
rows overlap; shared `gameplay_keep` is excluded):

| Actor category | Object folders | Skeletons | Triangle cmds | Textures | Animations |
|---|---:|---:|---:|---:|---:|
| NPC | 89 | 102 | 44,249 | 1,732 | 860 |
| Enemy | 43 | 51 | 10,636 | 468 | 312 |
| Boss | 10 | 17 | 8,032 | 201 | 193 |
| Prop | 18 | 21 | 6,602 | 272 | 165 |
| ItemAction (arrows, hook, etc.) | 16 | 21 | 6,301 | 248 | 208 |
| Not referenced by an actor profile | 14 | 14 | | | |

The last row holds the five Link objects and a handful of cutscene-only ones. In addition there are **289 object
folders without a skeleton** (pots, chests, signs, doors, set pieces): 1,205 display lists, ~35,800 triangle
commands, 254 of them with textures.

### 1.4 What counts as the "main models" for a credible replacement (proposal, not a decision)

The goal is to validate a replacement technique and then show a believable slice. Proposed tiers, ordered by
what each one proves:

| Tier | Models | What it validates |
|---|---|---|
| **0: validation kit** (about 15 models) | Link Human, Deku, Goron, Zora, Fierce Deity (21-limb Flex, equipment variants, eye/mouth texture swaps); Tatl (6 limbs, translucent glow); Anju (20 limbs), Clock Town guard (Soldier, 16), Happy Mask Salesman (18), Romani (22): "human NPC" rigs; Deku Baba (7) and Octorok (15): enemy rigs; Odolwa (51 limbs) as the extreme; pot, chest, sign: non-skeleton props | rigid-vs-flex binding, every seam type, texture-swap protocol, shadows and TAA interplay, prop path |
| **1: first playable slice** (Clock Town, about 3 days) | tier 0 plus Skull Kid, Kafei, Cremia, Mayor Dotour, Postman, Bomber kids, carpenters, Bombers, guards, Tingle, Deku Butler, scarecrow, ~10 common props | enough to judge consistency across a town |
| **2: common enemies and bosses** | the 43 enemy and 10 boss object folders | animation fit at scale; bosses use the biggest rigs |
| **3: everything** | 172 skeleton folders (205 skeletons) + 289 prop folders | full coverage |

Rough counts per tier: tier 0 is ~15 models with ~600 textures; tier 1 is ~45 models; tier 2 is ~100; tier 3 is
174 object folders with skeletons and about 289 without. These are the numbers a commission quote, a converter
run, or an art budget must be multiplied by.

### 1.5 Existing community coverage (the baseline we would be replacing)

MM Reloaded 4K v11.0.3 (`MM_Reloaded_v11.0.3_4K.o2r`) [measured]:

| Group | Textures | Pixels | Size | Typical edge length |
|---|---:|---:|---:|---|
| `alt/scenes` | 3,200 of 3,518 | 3,644 Mpx (714× the original) | 14.6 GB | 1024 px (1,435), 512 (817), 2048 (654), 4096 (75) |
| `alt/objects` | 4,131 of 4,766 | 1,795 Mpx | 7.2 GB | 512 (1,691), 256 (1,109), 1024 (865) |
| UI and other | 2,676 | 723 Mpx | 2.9 GB | 256 px (1,709) |
| **Total** | 10,007 | | **24.65 GB** | |

So about **318 world and 635 character textures are not covered by the pack at all**; a "fresh start" pass would
also have to deal with those. The pack's own notes credit about twenty upstream sources, including Nerrel's N64HD,
the OoT Community Retexture project and iQue textures, and neither `GhostlyDark/MM-Reloaded` nor
`GhostlyDark/MM-Reloaded-2S2H` nor `Nerrel/MMHD` declares a licence in its repository metadata [primary:
GitHub API], so default copyright applies.

### 1.6 The existing material classifier is not semantic

`mm/assets/custom/newdawn_materials/material-classification.json` labels the 3,200 pack textures stone 2,108,
plaster 410, other 317, grass 281, dirt 37, roof tile 17, wet 15, wood 12, metal 3. Its own `method` field says
"RGB hue/saturation, luminance energy, directional grain and periodic border difference; hypotheses, not
semantic recognition". Any plan that maps textures to material classes (section 3.1) needs a real, reviewed
labelling pass first.

---------------------------------------------------------------------------------------------------------------

## 2. How replacement works technically (question B)

### 2.1 The override mechanism

`ResourceManager::LoadResourceProcess` (libultraship `src/ship/resource/ResourceManager.cpp:111`) tries
`"alt/" + path` first for **every resource** when alternate assets are on, then falls back to the original. It is
not texture-specific. The 3DS Link mod proves it: its archive `1- 3DS Link.o2r` contains, all under
`alt/objects/`: 692 display lists, 321 vertex arrays, 269 skeleton and limb entries, 85 textures, across
`object_link_child/goron/zora/nuts/boy` and `gameplay_keep` (1,411 entries, 3.5 MB). Entries are XML
(`<Skeleton Type="Flex" LimbCount="21">`, `<SkeletonLimb ... ChildIndex SiblingIndex DisplayList1>`,
`<DisplayList>`, `<Vertex>`) built by HarbourMasters' `retro` tool (MIT).

2Ship and libultraship are CC0 / MIT, `retro` and `Torch` are MIT [primary: GitHub API].

### 2.2 What constrains a model replacement

1. **The animation data is the contract.** An N64 animation is a list of per-limb Euler rotations plus a root
   translation, indexed by limb. A replacement skeleton must keep the same limb count, hierarchy and joint
   pivots; extra bones are only possible if their pose is derived render-side from existing limbs (twist, hair,
   cloth), which stays inside "visual only". Hitboxes, collision and timing are untouched because those are read
   from actor code, not from the model. This is the hard-boundary rule in `AGENTS.md`.
2. **Rigid per limb, with a Flex escape hatch.** A limb draws its own display list under its own matrix. In a Flex
   skeleton the game allocates an array of matrices (`SkelAnime_DrawFlexOpa`, `z_skelanime.c:421`) that is
   addressable from segment `0x0D`, "so each limb can access any transform to ensure its meshes line up". The 3DS
   Link mod's hand display lists use exactly this (`<Matrix Path=">0x0d000280" Param="G_MTX_LOAD"/>`, then the
   limb mesh): vertex groups near a joint are drawn under either neighbouring limb's matrix, so triangles stretch
   across the joint. It is a **hard assignment per vertex group, not blending**.
3. **Skin limbs (real weights) exist for one skeleton.** All 46 `LimbType::Skin` limb resources in the archive sit
   in `object_horse_link_child` (Epona; `En_Horse`, `z_skin.c`); only one skeleton declares the Skin limb type.
   Nothing else in the game uses vertex weights.
4. **Display-list constraints:** F3DEX-style display lists, 16-bit integer vertex positions, at most 64 vertices
   per vertex load in this interpreter (`MAX_VERTICES 64`, `interpreter.h:262`), and the textures a limb uses are
   addressed through the display list, with eye/mouth/skin swaps done by segment pointers. A replacement has to
   keep those swap points (the 3DS mod ships `gLinkZoraEyesClosedTex`, `...EyesDownTex` and so on).
5. **Variants multiply the work.** Link Human has 87 display lists in the original (hands open, closed, holding
   a bottle, sword, shield; masks); the 3DS mod ships 692 display lists across five forms. Any replacement set
   must cover the same variant matrix or the game falls back to the original in mixed states.
6. **Budgets.** The interpreter transforms every vertex on one CPU thread (`docs/SMOOTH_CHARACTERS.md`), so
   triangle count costs CPU time, not just GPU time. MM3D Link at 8.4k triangles is ~4× the N64 total, still tiny
   by modern standards but multiplied across a crowded Clock Town. Textures are limited by the cache budget
   (`gNewDawn.Textures.CacheBudgetMB`, 2,048). Smooth characters (ND-012, PN patches) add only ~0.4 ms CPU and
   ~0.016 ms GPU in South Clock Town.

### 2.3 How the 3DS Link mod works, and what it does not do

Its skeleton is the original 21-limb Flex skeleton with the same child and sibling indexes
(`ChildIndex="2" SiblingIndex="9"` for the Zora waist limb); its limb display lists are Fast64-style
(`gLinkZoraSkel_bone013_gLinkZoraLeftShoulderLimb_mesh_layer_Opaque`, which I infer is Fast64 output, not a
confirmed fact). The author calls it "my first skeleton swap" [primary: GameBanana text]. The 3DS rig has 24-28
bones in a different tree, so the author had to **re-bind the 3DS mesh to the N64 limbs by hand** and live with
rigid segments. The result keeps every original animation, which is why it passes the hard boundary, and it
remains compatible with our smooth-character pass (`docs/SMOOTH_CHARACTERS.md`: the hat and boots get rounder from
behind, and the front view changes little because the model is already dense).

### 2.4 The material system (ND-009) and its limit

Normal, roughness and relief maps are generated from HD albedos the player has loaded and mounted above all mods
(`docs/RUNTIME_MATERIALS.md`). Two properties constrain every texture plan:

- The renderer binds companion maps only to albedos whose path starts with `alt/scenes/`
  (`Interpreter::BindMaterialMaps`). **Objects, characters and the native textures get no maps today.**
- Without an HD pack the generator "does nothing". All 2,209 map sets in the ROADMAP therefore depend on a
  24.65 GB community download that we cannot redistribute.

Consequence: a self-sourced texture pipeline should write its output as an `alt/` archive in the same layout
(like `newdawn-recolor.o2r` does), and extending map binding to `alt/objects/` is a prerequisite for per-model
normal and roughness maps.

### 2.5 What it would take to support higher-fidelity replacements

| Capability | Today | What is needed | Effort [estimate] |
|---|---|---|---|
| Rigid or Flex per-limb meshes, any triangle count | works (the 3DS Link mod) | tooling only | days per skeleton, mostly artistic |
| Per-model normal / roughness maps | maps bind to `alt/scenes/` only | bind to `alt/objects/`; character tangent frame from UV derivatives (N64 vertices have no tangents); generate maps for the replacement albedos | 1-2 weeks, low risk |
| **Smooth skinning** (linear blend, up to 3-4 bones per vertex) | none (rigid or Flex hard assignment) | a new skinned-vertex display-list command, a new resource type, a vertex-shader path that reads the Flex matrix array already sent for the actor, plus shadow-caster, TAA mask and smooth-character interplay | 3-6 weeks; **needs a one-week prototype** because the Flex array reaching the interpreter is an inference from the draw code, not something I ran |
| Extra bones (hair, cloth, twist) | none | render-side derived pose from existing limbs | 1-3 weeks per feature |
| Higher-resolution vertices (16-bit positions are enough, but UV and normal precision) | works | none | |
| Non-Flex (Normal, 40 skeletons) | each limb under its own matrix only | a palette from the limb matrices the interpreter sees; marker at skeleton end | included in the skinning work |

Fast64 (the Blender add-on the community uses to author display lists) is **GPL-3.0** [primary: GitHub API],
which is fine as an authoring tool (its output is not covered), but its README targets F3D with SM64 and OoT
modes; I found no MM-specific export. `retro` converts to and from o2r.

For comparison, Zelda 64: Recompiled (GPL-3.0) has a full mod API with model replacement mods
(YAZMT's Player Model Manager covers Link per form and equipment, shipped as NRM files) and `.rtz` HD texture
packs through RT64. It confirms the demand and the approach; it is a different engine, not something we can
reuse. NVIDIA RTX Remix, the other well-known "remaster by asset replacement" pipeline, targets DirectX 8/9
fixed-function games and does not apply to this renderer.

---------------------------------------------------------------------------------------------------------------

## 3. Texture sourcing options (question C)

Each option is judged against ND-008. Legend: ✅ shippable, 🟡 game-derived, must be generated on the player's
machine, ❌ cannot be used.

### 3.0 Proof of concept: what AI upscaling actually produces [measured]

Five RGBA16 originals from Clock Town and Termina Field (stone paving, brick, stone wall, grass, wood), decoded
from `mm.o2r`, run through four models and put next to MM Reloaded 4K. Images:
`poc/comparison.png`, `poc/two_pass_comparison.png`, `poc/comparison_pbrify.png`.

Speed (RX 7800 XT for Vulkan, 8 CPU threads for PyTorch; 400 random 32-pixel-class textures):

| Model | Hardware | Throughput | Whole world+character set (8,284 textures, 9.3 Mpx), 4× pass [estimate] |
|---|---|---:|---|
| Real-ESRGAN x4plus (ncnn-vulkan) | RX 7800 XT | 61 tex/s | ~2.5 min |
| Real-ESRGAN x4plus (ncnn-vulkan) | Intel UHD 770 iGPU | 7.8 tex/s | ~18 min |
| PBRify SPAN V4 (PyTorch CPU) | 8 threads | 122 tex/s, 73 kpx/s | ~2-3 min |
| PBRify RPLKSRd V3 (PyTorch CPU) | 8 threads | 34 tex/s, 20 kpx/s | ~8 min |
| Real-ESRGAN twice, 32→512 px (100 textures) | RX 7800 XT | 9.3 s per 100 | ~15 min [extrapolated, size mix unknown] |

What it looks like:

- **Real-ESRGAN x4plus:** sharper and hard-edged, with a photographic bias: brick mortar turns cold blue, stone
  gets black outlines, grass becomes leaf veins, wood becomes crumbling plaster. Two passes add resolution but
  not meaning.
- **animevideov3:** a smoothed copy of the original; not worth shipping.
- **PBRify RPLKSRd V3 and SPAN V4:** more natural material response and less contrast. The SPAN V4 grass is
  plausible; both stay soft. They remove banding and dithering well.
- **MM Reloaded 4K:** clearly richer than any of them, painterly, correctly structured (a human drew the
  stones). The gap does not close by running the same models harder; an upscaler is a restorer, not an
  author. A 32×32 source holds about 1,000 pixels of information.

### 3.1 CC0 / CC-BY PBR libraries mapped to material classes: ✅

Licences read from the sources [primary]: ambientCG: "All ambientCG assets are provided under the Creative
Commons CC0 1.0 Universal License. You can include the raw files in your project, for example a video game"
(<https://docs.ambientcg.com/license>). Poly Haven: "Our assets are all licensed as CC0 ... You can redistribute
them ... or even in a product you sell" (<https://polyhaven.com/license>); one third-party guide claimed a
redistribution restriction, but the licence page itself does not say so (its Terms of Service forbid scraping
the website and have a separate API term, so download the assets we pick by hand or via the documented API, not
by bulk crawling). Size [measured via each site's public API]: ambientCG 2,042 `Material` results; Poly Haven 869
textures and 522 models. cgbookcase lists "over 550 textures, CC0" [secondary].

What my test showed (`poc/comparison_cc0.png`): three ambientCG photogrammetry materials colour-matched to three
originals have excellent fine detail and real normal/roughness/height maps, but **the scale and style are
wrong**. One N64 brick tile shows about 6 large bricks; the CC0 brick tile shows about 30 small ones. Putting
the CC0 tile at the N64 footprint makes it busier, not better. The grunge and photographic colour variation do
not match the game's painted, simplified look. My picks were also arbitrary, which is the point: the quality
depends on per-texture art direction.

Open problems: (a) the 2,480 distinct world textures need real semantic labels, the current classifier will
not do (1.6); (b) many textures are unique art, not materials (murals, signs, emblems, carved reliefs, painted
flowers, dungeon decals); (c) vertex colours carry baked lighting, so the albedo must sit at a predictable
mid-grey; (d) scale must be authored per surface (UV density is a property of the original level geometry);
(e) the library must be stylised enough, and there is no large CC0 *stylised* PBR library (Kenney's and
OpenGameArt's hand-painted CC0 packs are small and uneven [secondary]).

Effort [estimate]: labelling and mapping ~2,500 distinct world textures by hand-reviewed rules: 2-4 weeks;
per-surface scale and tint tuning for the visible top few hundred: 3-6 weeks. Cost: time only.

### 3.2 AI upscaling on the player's machine: 🟡 (tool ✅)

Tool and weight licences [primary unless noted]:

| Tool / model | Licence | Notes |
|---|---|---|
| Real-ESRGAN code and RealESRGAN_x4plus weights | BSD-3-Clause (repo LICENSE; weights are published in the same repo's releases; no separate weight licence is stated) | trained on DF2K and OST data, provenance is the usual research datasets [not verified] |
| realesrgan-ncnn-vulkan | MIT | 46 MB bundle with models, runs on any Vulkan GPU, including iGPUs |
| **PBRify_Remix models (Kim2091)** | **CC0**, "trained exclusively on high quality CC0 content from ambientCG" | RPLKSRd V3 is 30 MB, SPAN V4 9 MB; also CC0 1× models that *generate* normal, roughness and height maps from an albedo |
| waifu2x, SwinIR, HAT, ESRGAN code | MIT, Apache-2.0, Apache-2.0, Apache-2.0 | weights vary per trained model |
| OpenModelDB overall (672 models) | CC-BY-NC-SA 227, CC-BY 123, WTFPL 73, CC-BY-NC 53, Apache-2.0 51, CC0 45, GPL-3 27, unlicensed 27, MIT 22, CC-BY-SA 14, BSD-3 9, Unlicense 1 | 42 % are non-commercial; 4x-UltraSharp is CC BY-NC-SA and its author says outputs are not for commercial use. Check each model |

Because the output is derived from Nintendo's textures, **it must be generated locally**, exactly like ND-009 does
for material maps. Shipping the CC0 weights and the pipeline is fine. Run time on the player's machine is
2-3 min (SPAN) to ~8 min (RPLKSRd) of CPU for a 4× pass over all 8,284 textures, and under 3 min on a decent
GPU through ncnn-vulkan. The first-run popup and cache machinery from ND-009 already exist.

Quality ceiling [measured, 3.0]: a clean, crisp, de-dithered 128 px, fine for moderate distances, and an
excellent *input* for the existing recolour and material generators, but not "really modern" by itself at
close range, and it hallucinates. A second 4× pass reaches 512 px with more hallucination. A mixed pipeline
(restore to 4×, then detail-inject from a CC0 stylised library, section 3.7) is the ceiling I would expect for
a local-only approach; I have not tested it.

### 3.3 Generative AI texture synthesis: ❌ as a primary source, 🟡 or ✅ in narrow cases

- **Models that are shippable [primary: HF model cards via API]:** FLUX.1 [schnell] Apache-2.0; SDXL base 1.0
  CreativeML OpenRAIL++-M; SD 3.5 under the Stability AI Community Licence (free under USD 1M revenue); FLUX.1
  [dev] is non-commercial ("other").
- **Output rights:** the US Copyright Office's January 2025 Part 2 report concludes that prompts alone do not make
  the user an author, and that purely AI-generated output is not copyrightable
  (<https://copyrightalliance.org/ai-report-part-2-copyrightability/>, [secondary summary]). Our "shippable"
  rule needs CC0/CC-BY/etc.; uncopyrightable output is effectively public domain in the US but we could not
  *assert* a licence, and other jurisdictions differ.
- **Training-data risk is unresolved:** in *Getty Images v Stability AI* the UK High Court (4 Nov 2025) rejected
  the secondary-infringement claim but found limited trademark infringement for watermarked outputs and left the
  training question open (<https://www.aippi.org/news/getty-images-v-stability-ai-uk-high-court-finds-no-secondary-copyright-infringement-in-landmark-genai-ruling/>
  [secondary]).
- **Consistency:** the hard part. A diffusion model has no stable "Majora's Mask" look unless guided by the
  original textures (img2img/ControlNet), and then the output is game-derived (🟡) and needs an NVIDIA/AMD GPU
  with several GB of VRAM and seconds per texture, which does not fit "generate at first launch".
- **Where it could fit:** one-off pre-generation of *generic* material tiles (no Nintendo input), reviewed by
  a human, with the prompt, seed and model licence committed. The result is shippable but has no copyright
  protection and the consistency work is the same as hand-authoring.

### 3.4 Commissioning or hand-authoring: ✅

Under ND-008 this is clean if the artist licenses CC0/CC-BY and the pipeline is committed. A reasonable unit is
the **hero set**: the top few hundred textures by screen coverage (an in-game coverage measurement has not
been done yet and is cheap to add), authored at 512-1024 px as stylised hand-painted or procedural albedos with
normal/roughness. Effort [estimate, no sourced rate]: a skilled artist at 1-3 h per 1024 px tile means
300-900 h for 300 textures; procedural Blender/Material Maker recipes (a script in the repo also satisfies the
rule) cut the per-variant cost once a class recipe exists. I found no sourced per-texture price; get quotes.

### 3.5 Community packs: ❌ to ship, ✅ to recommend and interoperate

MM Reloaded has no licence file, credits ~20 third-party sources and is 24.65 GB; Henriko's MM3D 4K pack
"does not allow redistribution" per the 3DS Link mod text; the 3DS Link and HD Moon mods are
**CC BY-NC-ND 4.0** (<https://gamebanana.com/mods/644553>, <https://gamebanana.com/mods/640438>, read through
GameBanana's API) and are themselves rebinds of Nintendo's MM3D models. Our current design already interoperates
with them (alt archives, ND-009 mounts above mods). The only route to ship any of it is written permission from
every credited author, which I would not plan around. A lighter option is to ask GhostlyDark and Nerrel to
publish an explicit licence (even CC-BY-NC-SA would settle the "recommend" question), without us depending on it.

### 3.6 Procedural and scripted generation: ✅

The rule allows assets "made by a script in the repository". New Dawn already does this for caustics, grass
tufts and the torch-fire flipbook. For textures this means Blender procedural materials, Material Maker (MIT) or
numpy recipes producing a stylised class library (brick, cobble, plaster, wood, grass, dirt, tile, cloth, metal)
that is tuned once to the game's proportions. It sidesteps the CC0 scale and style problem, at the price of
authoring the recipes.

### 3.7 Hybrids

- **H1, restore + detail injection (not tested):** run PBRify-class restore to 4× locally; then add high-frequency
  detail from a shipped CC0 or procedural class library, weighted by the original's luminance. Preserves layout,
  colour and motifs (so characters and places stay recognisable) and gets modern micro-detail without hallucinated
  structure. Output stays 🟡 (game-derived), the detail library ✅.
- **H2, hero replacements over a local base:** H1 for all textures, plus shipped hand-authored replacements for the
  top-N most visible.
- **H3, interoperate:** keep the community pack as an optional upgrade layered above, exactly as ND-002/ND-009 do.

---------------------------------------------------------------------------------------------------------------

## 4. Model sourcing options (question D)

### 4.1 MM3D models converted at runtime from the player's own 3DS dump: 🟡

A clean-room converter exists for rooms (`tools/newdawn/mm3d/`, based on noclip.website's MIT format notes). For
characters I measured the user's dump with that same reader [measured, `scripts/mm3d_chars.py`]:

- **460 actor archives, 1,448 CMB models (1,427 parse with the current reader, 98.5 %), 3,237 CSAB animation files,
  456 CMAB material animations.** The 21 failures are attribute-buffer mismatches, not unknown formats.
- **Geometry:**

| Character | MM3D bones / triangles / textures (largest) | N64 limbs / triangles |
|---|---|---|
| Link Human | 26 / 8,393 / 33 (128²) | 21 / 2,176 |
| Goron Link | 24 / 2,906 | 21 / 1,637 |
| Zora Link | 28 / 3,115 | 21 / 1,023 |
| Deku Link | 24 / 2,410 | 21 / 1,018 |
| Fierce Deity | 24 / 2,992 | 21 / 835 |
| Anju | 20 / 1,112 / 6 (128²) | 20 / 593 |
| Soldier | 16 / 949 / 4 (128²) | 16 / 445 |
| Happy Mask Salesman | 20 / 2,494 | 18 / 834 |
| Majora's Mask (boss) | 18 / 372 | 18 |
| Majora demon model (`majora_demon.cmb`, the Wrath form) | 113 bones / 4,742 | 27 limbs (`gMajorasWrathSkel`) |
| Odolwa | 40 / 2,157 | 51 / 891 |

  MM3D characters are 1.5-4× denser, skinned smoothly (skinning mode 2 with up to 3 bones per vertex on the
  large pieces), and their textures are as small as the originals (Link's largest is 128×128). **MM3D buys
  better geometry, not better textures**; the 3DS "4K" look people know comes from Henriko's separate
  non-redistributable pack.
- **Skeleton compatibility** (`scripts/skel_compare*.py`; N64 hierarchy from the o2r limb child/sibling
  indexes, MM3D hierarchy from the CMB bone parents, compared as rooted trees; archives matched only by name):

| Result over the 214 N64 skeletons | Count |
|---|---:|
| Identical rooted hierarchy in the same-named MM3D archive (necessary, not sufficient, for a 1:1 limb map) | **65** |
| N64 tree = MM3D tree minus a few extra leaf or contractible bones | 17 |
| Different bone count or tree (includes all five Link forms) | ~76-90 |
| No MM3D archive of that name (many shared `gameplay_keep` skeletons, and renamed objects) | 45 |
| Single-limb (nothing to bind) | 8 |
| N64 skeleton not parsed by my script (skin or odd limbs) | 3 |

  Identical examples: Anju, Soldier, Majora's Mask, Dinolfos, Dog, Takkuri, Wolfos. Different: Link ×5 (21 vs
  24-28), Romani 22/25, Cremia 21/23, Happy Mask Salesman 18/20, Odolwa 51/40, Tingle 32/22.
- **Animation:** the N64 animation data stays the driver (that is what the hard boundary demands), so the 3DS
  CSABs are not used. The converter must bake the 3DS mesh into the N64 rest pose, assign each vertex to its
  dominant N64 limb (or to a seam pair, using Flex) and emit display lists plus the eye/mouth texture swap
  points. This is the work the 3DS Link mod author did by hand.
- **Materials and effects:** PICA combiners, material animations (456 CMAB) and eye and mouth swaps have to be
  mapped onto the N64 texture-segment protocol and onto our shaders; room conversion already decodes ETC1 and
  the other PICA texture formats.
- **Legal:** the same as 2Ship's own extraction: the player supplies the dump, we ship only the converter.
  Practical cost: players need an MM3D dump as well as an N64 ROM.
- **Effort [estimate]:** first character class (Link Human) 3-6 weeks including rebinding, textures and shadow/TAA
  verification; ~1-2 weeks per further class once the mapping is automated; the **identical-hierarchy
  characters (65 skeletons) are the cheap first batch**. A one-week spike on Anju and the Soldier would
  replace this estimate with a measurement.
- **Risk:** rigid segments at the joints (smooth characters rounds silhouettes but cannot fill a gap), 3DS rest
  poses that do not match the N64 pivots, 45+ unmatched skeletons needing another source.

### 4.2 Community remodel projects

- **3DS Link / 3DS Equipment / HD Moon** (2Ship, GameBanana): five forms of Link, equipment, the Moon. CC BY-NC-ND,
  so not shippable and not modifiable by us; they stay a separate download (current README, ND-002).
- **Zelda 64: Recompiled model mods** (Thunderstore: Player Model Manager API, Jackie Quilt model, many Link
  models) target a different engine and their licences vary per mod [not reviewed].
- **Other full-cast remodels:** I found none for MM on 2Ship. A fan Unreal Engine 5 rebuild of Clock Town and
  Termina Field (Benoit Bourgerie) and a "Majora's Mask Rebirth" UE4 recreation exist as demos, not as
  playable assets or releasable models.

### 4.3 Commissioned or hand-made models bound to the existing skeletons: ✅ (with the IP caveat)

Market ranges [secondary, vendor marketing pages]: a stylised rigged character from a freelancer or small studio
runs USD 1,000-5,000, up to 2,500-20,000 for cinematic-grade work, and USD 2,000+ for a simple stylised
character including rigging; typical turnaround is 5-10 days for a simple stylised character
(<https://rocketbrush.com/blog/3d-character-art-prices-guide>, <https://pixune.com/blog/3d-model-cost/>). Our job is
harder than a generic commission: the model must be built on a **fixed rig** (limb pivots and rotation limits from
the N64 skeleton), keep Link's 87 display-list variants, and be authored for Fast64/`retro`.

Effort [estimate]: tier 0 (15 models) = 60-120 artist-days; tier 1 (45) = 200-400; tier 3 (about 460 object
folders incl. props) is a studio-year-scale effort. At the quoted rates the 15-model kit would cost very roughly
USD 15,000-60,000 if done externally. These numbers are order-of-magnitude only; the quote must come from a real
artist who has seen the rig.

### 4.4 AI 3D generation: mostly ❌

| Model | Licence | Fit |
|---|---|---|
| TRELLIS.2 (Microsoft, Dec 2025) | code and weights MIT [primary]; README lists nvdiffrast and nvdiffrec under their own licences; the default pipeline (`pipeline.json`) loads `briaai/RMBG-2.0` for background removal (its Hugging Face licence field is "other"; BRIA's terms are reported as non-commercial [secondary]) and `facebook/dinov3` (DINOv3 licence: commercial use allowed with attribution "Built with DINOv3") | needs an NVIDIA GPU with ≥ 24 GB; meshes can have "small holes or minor topological discontinuities" [primary: model card] |
| TRELLIS v1, TripoSR, TripoSG | MIT | older or smaller; lower quality |
| Step1X-3D | Apache-2.0 | |
| Stable Fast 3D | Stability Community Licence (commercial under USD 1M revenue) | |
| **Hunyuan3D 2.1** | Tencent licence: **does not apply in the EU, UK and South Korea; using outputs outside the "Territory" is unlicensed; outputs may not improve another AI model** [primary: LICENSE text] | unusable for a worldwide open project |
| UniRig (auto-rigging) | MIT code; checkpoint terms vary | predicts its *own* skeleton |

Structural problems, independent of licence: (a) the output is an unstructured triangle soup with no relation
to the N64 skeleton, so it needs retopology (Instant Meshes is BSD-style, check its LICENSE.txt), UV unwrap,
texture bake and a **weight transfer onto the fixed 21-limb rig** (which becomes rigid binding anyway); (b) to
generate Link or Anju you must feed it images of Link or Anju, so the result is derived from Nintendo's art and
cannot be generated on a player's machine (24 GB VRAM). The same IP issue applies to every other route for
named characters (section 8). AI generation is most defensible for **generic props** (barrels, crates) where
no Nintendo likeness is involved and the style is easy to match.

### 4.5 Extending our render-time approach (#109 and ND-012)

Already in main: PN patches round limbs (+0.016 ms GPU, +0.4 ms CPU). The next steps stay within "visual only"
and need no external asset:

- **Displacement / relief on characters** (cloth weave, armour, scales) from the generated height maps, once
  maps bind to objects (2.5).
- **Runtime-baked normal maps for characters**: derive normal and roughness from whatever albedo wins, as
  ND-009 does for the world. Needs the object-path binding and a character tangent frame.
- **Better shading**: subsurface for skin, anisotropic highlights for hair and metal, a hair and fur shell pass
  (all visual only).
- **Procedural micro-geometry** is out: it cannot add real silhouette beyond the N64 vertices.
Ceiling: visibly better than today, but a 400-triangle NPC stays a 400-triangle NPC; this closes part of the gap
between N64 and "remake" geometry without changing the models.

### 4.6 Hybrids

- **MM3D geometry + our materials + smooth skin:** MM3D rebinding gives mid-density geometry; our maps and PN
  rounding raise the finish; renderer-side LBS skinning removes joint seams.
- **Commissioned hero characters + MM3D or render-time for the rest:** pay for Link ×5, Tatl and a small cast that
  carry the look; use MM3D or render-time upgrades for the long tail.
- **Own low-poly "stand-in" set for validation:** a handful of simple CC0/own models (a cube-man NPC with
  the real 21-limb rig) to validate skinning and binding before spending on art.

---------------------------------------------------------------------------------------------------------------

## 5. Art direction: what "really modern" should mean (question E)

The AGENTS.md purpose is a game that "looks reborn ... comparable to a current-generation 3D remake", with the
world, places and characters still recognisable. References:

- **Link's Awakening (Grezzo/Nintendo, 2019):** deliberately *not* a 1:1 re-skin; the graphic refining director
  described a diorama world where Link is a figurine of about 10 cm, with toy-like plasticine characters,
  tilt-shift depth of field, saturated colour and soft lighting
  (<https://nintendoeverything.com/graphic-refining-director-yoshiki-haruhana-on-reimagining-the-visuals-of-zelda-links-awakening/>,
  <https://en.wikipedia.org/wiki/The_Legend_of_Zelda:_Link%27s_Awakening_(2019_video_game)>). Lesson: a modern
  remake picks one coherent material language and commits to it, even if the geometry stays simple.
- **Ocarina of Time 3D and Majora's Mask 3D:** the official "same design, higher fidelity" baseline:
  denser meshes and smooth skinning, small textures by modern standards (we measured 16²-128² on Link). This is
  the 3DS-era target our MM3D conversion would reach; it is a clean upgrade but not a current-gen one.
- **Fan remakes:** Benoit Bourgerie's Unreal Engine 5 Clock Town and Termina Field are praised for art direction
  that "pays homage to the cartoon and animated film roots" with Nanite and Lumen detail
  (<https://zeldacentral.com/how-a-solo-creator-reimagined-majoras-mask-in-unreal-engine-5>,
  <https://www.tweaktown.com/news/106819/legend-of-zelda-fan-spends-year-recreating-classic-n64-game-and-the-result-looks-stunning/index.html>);
  other fan work goes for a realistic or Ghibli-like finish. Lesson: coverage goes to the *style*, not to
  raw polygon count.

A concrete reading for Majora's Mask (my synthesis [estimate], for discussion): keep the **hand-painted, slightly
exaggerated proportions and the muted, uneasy palette**; move from flat low-res albedo to **layered material
detail with real roughness, normals and parallax** (the direction our renderer already takes); keep
silhouettes and shapes recognisable but smoother (PN patches, extra edge loops where they help); avoid photoreal
scans that fight the painted look (the CC0 test in 3.1). Three targets worth naming explicitly with the
maintainer: **A "painterly MM3D+"** (MM3D shapes, richer textures), **B "stylised current-gen"** (LA2019-like
material language, hand-painted PBR), **C "grounded remake"** (the UE5 fan look). They imply different asset
sources, so the choice drives the plan.

---------------------------------------------------------------------------------------------------------------

## 6. Per-option analysis (question F)

Scales: effort in person-weeks or person-months [estimates]; quality ceiling is my judgement from the proofs
above; "Legal" uses ND-008 (✅ ship, 🟡 local only, ❌ not allowed).

| # | Option | Legal / distribution | Quality ceiling | Consistency | Effort and cost | Technical risk | Player experience | Combines with our renderer |
|---|---|---|---|---|---|---|---|---|
| T1 | CC0 PBR library mapped to classes | ✅ CC0; attribution optional | High detail, **style-risky** (3.1 test) | low unless art-directed | 2-4 wk labelling + 3-6 wk tuning; free assets | low | +0.5-1.5 GB texture download | PBR maps come ready (skip generator); POM works as is; needs scale control |
| T2 | Local AI restore (PBRify-class) | 🟡 tool ✅ (CC0 weights) | Medium: clean 128-512 px, hallucination risk | medium-high (same model on everything) | 1-3 wk to integrate; free | low; CPU or Vulkan | 2-8 min first run, cache ~0.5-2 GB | Feeds ND-009 generators; no pack needed |
| T3 | Generative AI synthesis | ✅ only for generic tiles; no copyright | Medium-high but unstable | low | days to prompt, weeks to curate | high (legal, style) | n/a | |
| T4 | Commission / hand-paint hero set | ✅ if CC0/CC-BY | **Highest, consistent** | high | 300-900 h per 300 textures; quotes needed | low | 100-500 MB | PBR authored directly |
| T5 | Community packs | ❌ to ship; ✅ to recommend | Already high (1-2k px, hand-processed) | high within a pack | 0 | low | 24.65 GB download | ND-009 works today |
| T6 | Procedural class library (scripts) | ✅ made by script | Medium-high, consistent | high | 2-4 wk per class set | medium (tuning) | small (recipes) | Native fit for PBR maps |
| T7 | Hybrid H1: local restore + CC0/procedural detail | 🟡 base, ✅ detail | Medium-high, layout preserved | medium-high | T2 + T6 | medium (untested) | 3-10 min first run | Strong fit with ND-009 |
| M1 | MM3D runtime conversion | 🟡 | Better geometry; textures stay small; "3DS-class" | high for what converts | 3-6 wk first class; ~1-2 wk per later class | medium-high (rebind, seams) | needs an MM3D dump; conversion run once | Works with smooth characters; maps need object binding |
| M2 | Community remodels (3DS Link etc.) | ❌ to ship (NC-ND) | 3DS-class | high within a mod | 0 | low | separate download | already supported |
| M3 | Commissioned models on fixed rigs | ✅ if CC0/CC-BY; **IP caveat** | Highest | high | ~USD 15-60 k for the 15-model kit; far more for all | low-medium (rig fit) | +50-300 MB | best with skinning support |
| M4 | AI 3D generation | mostly ❌ (licences, IP, hardware) | Variable | low | high retopo/rig cost | high | n/a | |
| M5 | Render-time upgrades (displacement, maps, shading) | ✅ our code | Visible but capped by the original silhouettes | high | 2-6 wk | low-medium | none | by definition |
| M6 | Hybrid: MM3D long tail + commissioned heroes + skinning | mixed | High | medium-high | M1 + M3 + skinning (3-6 wk) | medium | as M1 | all of the above |

---------------------------------------------------------------------------------------------------------------

## 7. What is unverified, and cheap ways to close it

1. **Texture coverage by screen area.** I did not measure how much of a typical frame the top 100/300/1,000 textures
   cover. This decides whether "a pass on the entire game" or "a pass on what matters" is the right scope.
   Cheap: log texture draws per frame in the interpreter over a scripted tour of ten scenes.
2. **H1 (restore + detail injection) is a hypothesis.** A one-day prototype on the five test textures would show
   whether it beats both pure upscaling and the pack.
3. **The flex-palette skinning path** (2.5) rests on reading `z_skelanime.c` and the 3DS mod's display lists. A
   prototype that skins one Link limb pair in the vertex shader would confirm it in about a week.
4. **MM3D rebind quality at joints.** Unknown until a spike on Anju or the Soldier (identical hierarchy) is rendered.
5. **My skeleton comparison compares tree shapes, not joint positions or rest poses.** "Identical hierarchy" is
   necessary for a 1:1 map, not sufficient.
6. **Real-ESRGAN weights' training-data provenance** and the **OpenModelDB** per-model weights are not audited;
   only PBRify's "CC0-only" training claim was read (and not independently checked).
7. **Sourced prices** for texture and character commissions: only vendor marketing ranges were found.
8. **Windows and macOS** were not tested for any of the tools above; the upscalers run on both through Vulkan or
   PyTorch, but nothing here was run there.
9. **MM3D coverage beyond actors** (props as `zelda2_*_obj`, scene-embedded actors) was not mapped.
10. The 3DS Link mod's CC BY-NC-ND notice and its relation to Nintendo's original assets is something I only
    read; I am not offering legal advice.

---------------------------------------------------------------------------------------------------------------

## 8. A decision the asset rule does not cover: Nintendo's characters themselves

ND-008 answers "can we redistribute this file". It does not answer "may we distribute a new model of Link". A
commissioned or AI-made Anju, even with every polygon and pixel ours, is a derivative depiction of Nintendo's
copyrighted character, and Nintendo has a long record of takedowns of fan games and remakes (for instance the
2020 DMCA against *The Legend of Zelda: The Missing Link*,
<https://torrentfreak.com/nintendos-lawyers-nuke-the-missing-link-fangame-with-copyright-complaint-201013/>).
The decomp ports themselves stay distributed as CC0 code with no assets, and I found no action against them.
ND-002 sidesteps the question by leaving all character and texture art as separate community downloads. Any
plan that *ships* character art (commissioned, AI-made or hand-made) changes that posture. Local generation from
the player's own dump (🟡) does not. This is a call for the maintainer, ideally with a lawyer's opinion, not
something I can settle here.

---------------------------------------------------------------------------------------------------------------

## 9. Sources

**Verified from the primary page or repository (fetched during this research)**
- ambientCG licence: <https://docs.ambientcg.com/license>; API <https://ambientcg.com/api/v2/full_json?type=Material&limit=1>
- Poly Haven licence: <https://polyhaven.com/license>; API <https://api.polyhaven.com/assets?t=textures>
- Real-ESRGAN: <https://github.com/xinntao/Real-ESRGAN> (LICENSE BSD-3; release v0.2.5.0 models and ncnn bundle);
  <https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan> (MIT)
- PBRify_Remix (CC0, trained on ambientCG): <https://github.com/Kim2091/PBRify_Remix>; model data
  <https://github.com/OpenModelDB/open-model-database> (672 models, licence tally)
- Hunyuan3D 2.1 licence: <https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE>
- TRELLIS (MIT) <https://github.com/microsoft/TRELLIS>; TRELLIS.2 <https://github.com/microsoft/TRELLIS.2> and
  <https://huggingface.co/microsoft/TRELLIS.2-4B> (including `pipeline.json` naming `briaai/RMBG-2.0` and
  `facebook/dinov3-vitl16-pretrain-lvd1689m`); DINOv3 licence
  <https://ai.meta.com/resources/models-and-libraries/dinov3-license/>
- HF model-card licences via API: FLUX.1-schnell (Apache-2.0), FLUX.1-dev (non-commercial),
  SDXL base (OpenRAIL++), SD 3.5 large and Stable Fast 3D (Stability Community Licence)
- GitHub licence API: Fast64 (GPL-3.0), Zelda64Recomp (GPL-3.0), UniRig, TripoSR, TripoSG (MIT), Step1X-3D
  (Apache-2.0), 2Ship (CC0-1.0), `HarbourMasters/retro` and `Torch` (MIT), libultraship (MIT); GhostlyDark
  MM-Reloaded, MM-Reloaded-2S2H and Nerrel/MMHD report no licence
- GameBanana API: 3DS Link <https://gamebanana.com/mods/644553> and HD Moon <https://gamebanana.com/mods/640438> (CC BY-NC-ND 4.0)
- noclip.website (MIT) <https://github.com/magcius/noclip.website>
- This repository: `AGENTS.md`, `docs/DECISIONS.md` (ND-002, 008, 009, 012), `docs/RUNTIME_MATERIALS.md`,
  `docs/SMOOTH_CHARACTERS.md`, `docs/MM3D_COLLISION_TRIAL.md`, `tools/newdawn/mm3d/README.md`,
  `libultraship/src/ship/resource/ResourceManager.cpp`, `mm/src/code/z_skelanime.c`, `mm/src/code/z_skin.c`

**Secondary (summaries, news, vendor pages)**
- USCO Part 2 summary <https://copyrightalliance.org/ai-report-part-2-copyrightability/>
- Getty v Stability AI <https://www.aippi.org/news/getty-images-v-stability-ai-uk-high-court-finds-no-secondary-copyright-infringement-in-landmark-genai-ruling/>
- Link's Awakening visual direction: <https://nintendoeverything.com/graphic-refining-director-yoshiki-haruhana-on-reimagining-the-visuals-of-zelda-links-awakening/>,
  <https://en.wikipedia.org/wiki/The_Legend_of_Zelda:_Link%27s_Awakening_(2019_video_game)>
- UE5 fan remake <https://zeldacentral.com/how-a-solo-creator-reimagined-majoras-mask-in-unreal-engine-5>,
  <https://www.tweaktown.com/news/106819/legend-of-zelda-fan-spends-year-recreating-classic-n64-game-and-the-result-looks-stunning/index.html>
- Zelda 64: Recompiled, Player Model Manager <https://thunderstore.io/c/zelda-64-recompiled/p/YAZMT/Player_Model_Manager/>
- MM Reloaded <https://evilgames.eu/texture-packs/mm-reloaded.htm>, <https://github.com/GhostlyDark/MM-Reloaded>;
  Henriko MM3D 4K <https://henrikomagnifico.com/zelda-majoras-mask-3d-4k>
- Prices <https://rocketbrush.com/blog/3d-character-art-prices-guide>, <https://pixune.com/blog/3d-model-cost/>
- Free CC0 texture roundups (cgbookcase, Kenney) <https://app.cinevva.com/guides/free-textures-hdris-materials>
- Nintendo and fan games <https://en.wikipedia.org/wiki/Nintendo_and_fan_games>
- RTX Remix compatibility <https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/introduction/intro-compatibility.html>
- Instant Meshes <https://github.com/wjakob/instant-meshes>

## Appendix: reproduce the numbers

`scripts/` (run with `python3 -I`; the PyTorch ones with `poc/venv/bin/python -I`):
`types.py`, `breakdown.py`, `texdims.py`, `skeletons.py`, `tris.py`, `actors.py` (inventory);
`mm3d_chars.py`, `skel_compare.py`, `skel_compare2.py`, `skel_compare3.py` (MM3D, reads the local dump and
prints numbers only, nothing is extracted); `otex.py`, `pbrify_run.py`, `pbrify_bench.py`, `cc0_match.py`
(proofs of concept); `fetch.py` (licence pages). Outputs: `objects_summary.json`, `object_tris.json`,
`actor_profiles.json`, `mm3d_actor_summary.json`, `skel_n64.json`, `skel_compare_detail.json`.
`poc/` holds the downloaded tools (Real-ESRGAN ncnn bundle, PBRify weights, a CPU PyTorch venv, three ambientCG
materials) and derived images; it is outside the repository and must stay out of it.
