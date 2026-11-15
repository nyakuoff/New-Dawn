# Options, recommendation, decisions needed

Companion to `MODELS_TEXTURES_REPORT.md` (evidence, citations, measurements). This file is the side-by-side view of the realistic
end-to-end strategies, then my recommendation and the decisions that are yours. Effort figures are my estimates
(person-weeks of focused work) and carry the uncertainty stated in the report, section 7.

Legend: ✅ shippable under ND-008, 🟡 generated on the player's machine from their own dump (never shipped),
❌ cannot be used. "IP caveat" means the asset is ours but depicts Nintendo's characters or places (report, section 8).

## What the numbers say, in six lines

- World + character art is **8,284 textures (about 6,450 distinct), 9.3 Mpx in total**; models are **214 skeletons
  in 174 object folders**, median 17 limbs, and all five Link forms share one **21-limb Flex skeleton**.
- The community pack covers about 91 % of world and 87 % of character textures, is **24.65 GB** and has **no
  licence**. New Dawn's whole material system only works if a player has such a pack (ND-009).
- The 3DS Link and HD Moon mods are **CC BY-NC-ND**: not shippable, not even adaptable.
- Models can be replaced today with per-limb meshes (what the 3DS Link mod does). **Smooth skinning is not
  supported** (only Epona uses weights); adding it is a renderer job of a few weeks.
- Of the 214 skeletons, **65 have an identical hierarchy in MM3D**, 17 more are "N64 plus leaf bones"; Link's five
  forms and most NPCs with extra bones need re-binding. MM3D textures are small, so it improves geometry only.
- A 4× AI upscale of a 32×32 original (proof of concept) is a good restorer, not a modern texture: it invents
  detail and tops out at 128-512 px. CC0 photo libraries have the detail but the wrong scale and style.

## Side-by-side

| | **A. Local restore** | **B. MM3D models, local textures** | **C. Own stylised library + hero set** | **D. Commissioned character set** | **E. Interop only (status quo +)** |
|---|---|---|---|---|---|
| **One-line idea** | At first run, a CC0-trained upscaler (PBRify-class) restores every world and character texture from the player's dump; our recolour and material generators run on the result. No community pack needed. | Convert MM3D characters from the player's 3DS dump, rebind them to the N64 limbs so the original animations drive them; textures from plan A. | Author a stylised CC0/procedural material library plus a hand-made hero set (top-N textures by screen area); the rest from A. | Commission (or hand-make) tier-0/1 models on the fixed rigs and ship them under CC-BY/CC0, plus skinning support. | Keep community packs as optional separate downloads (ND-002); spend the effort on the renderer (object-path maps, skinning, render-time upgrades). |
| **Textures** | all 8,284, locally, 128-512 px | local (A) | hero set shipped, rest local | n/a | the player's pack |
| **Models** | unchanged (+ #109 smoothing) | 3DS-class geometry for the cast we can convert | unchanged | hero characters | unchanged / community mods |
| **Legal (ND-008)** | 🟡 (tool and weights ✅, CC0) | 🟡 (converter ✅) | ✅ library and hero set (IP caveat for motifs); 🟡 the rest | ✅ files, **IP caveat on every character** | ✅ no change |
| **Quality ceiling** | Medium: clean and sharper, hallucination risk, soft at close range | Better silhouettes and skinning than N64; 3DS-era textures | High and **consistent** where authored | Highest | High if the player has the pack, otherwise today's look |
| **Consistency** | Medium-high (one model on everything) | High per converted character | High | High | Varies by pack |
| **Effort** | 4-8 wk (pipeline, UI, cache, tests) | 3-6 wk first class (Link) + ~1-2 wk per later class; identical-hierarchy batch first | 3-6 months (library recipes, labelling, hero set) | 15-model kit: 60-120 artist-days, plus 3-6 wk renderer | 4-8 wk renderer work |
| **Cost** | engineering only | engineering only | engineering + artist time | roughly USD 15-60k for the kit (vendor-range estimate), more for the cast | engineering only |
| **Technical risk** | Low | **Medium-high**: rigid joints, rest-pose mismatch, 45+ skeletons with no MM3D counterpart | Medium (tuning and consistency) | Low-medium (rig fit, 87 Link variants) | Low |
| **Animation compatibility** | n/a | Keeps N64 animation data; needs a per-skeleton limb map | n/a | Built on the fixed rig | n/a |
| **Player experience** | 2-8 min one-time popup (CPU or any Vulkan GPU), ~0.5-2 GB cache | needs a 3DS dump too; one-time conversion; ~100-300 MB cache | +0.1-1 GB download | +50-300 MB download | up to 24.65 GB of separate downloads |
| **Fits our renderer** | Feeds ND-009 directly; make maps bind to objects | works with smooth characters; maps need object binding | PBR maps authored directly | best with real skinning | already works |
| **Biggest unknown** | Does detail injection (H1) lift it to "modern"? | Joint quality after rebinding | How much art coverage a team can afford | The IP posture | Players without the pack get no benefit |

## What I would not pursue

- **AI 3D generation for named characters.** The licences are the least of it (Hunyuan3D 2.1 excludes the EU, UK
  and South Korea; TRELLIS.2 needs 24 GB NVIDIA and pulls non-commercial or custom-licence components): the output
  has no relation to the 21-limb rig, and generating Link or Anju means feeding Nintendo-derived images in.
  Acceptable only for generic props, as a drafting aid with human cleanup.
- **Generative texture synthesis as a primary source.** Unstable style, no copyright to license, and
  game-guided img2img is game-derived (cannot be shipped, too heavy to run on the player's PC).
- **Photo-scan CC0 libraries applied directly.** The test shows the scale and style mismatch; only useful as a
  detail source under a stylised base.
- **Anything NC-ND or unlicensed** (3DS Link, HD Moon, MM Reloaded, Henriko's pack): keep them as optional
  separate downloads.

## Recommendation (for discussion, not a decision)

A **layered path**, cheapest and most reversible steps first, with a gate before each money- or IP-sensitive step.

**Step 0, one to two weeks of spikes (decide with data):**
1. Measure texture coverage by screen area over ten scripted scenes. This tells us whether a full pass or a
   top-300 pass is what the maintainer's "textures that seriously could be better" actually means.
2. Prototype detail injection (restore + CC0/procedural detail, hypothesis H1) on the five test textures and
   compare with the pack. This is the first honest test of whether a purely local pipeline can look modern.
3. MM3D spike on Anju and the Clock Town guard (identical hierarchy): convert, rebind to the N64 limbs, render
   with the original animations. It measures joint quality and the real converter cost.
4. Skinning prototype: skin one Link limb pair in the vertex shader from the Flex matrix array. It settles
   whether real blended skinning is a few weeks or a rewrite.

**Step 1, plan A (4-8 weeks):** make New Dawn independent of a 24 GB pack: local restore, then object-path
material maps. Everything stays 🟡/✅ and reversible, and the community pack remains an optional upgrade above it.
This is also what a "fresh start" for textures needs as its floor: every one of the 8,284 world and character
textures gets a clean modern baseline, including the 318 world and 635 character textures the pack never covered.

**Step 2, choose the model path from the spike results:** plan B for the long tail of NPCs and enemies if the
spike joints look good; plan D selectively for the few characters that carry the look (Link ×5, Tatl, maybe
the Clock Town cast) if you accept the IP posture; renderer skinning support if either needs it.

**Step 3, plan C hero textures:** author the stylised library and the top-N set where the coverage data says
they matter, over the local base.

If the first spikes disappoint, **plan E** is the zero-risk fallback and already roughly what ND-002 chose.

## Decisions the maintainer must make

1. **Visual target** (report, section 5). Which of: (A) "painterly MM3D+" (MM3D shapes, richer textures); (B)
   "stylised current-gen" (Link's Awakening-style one coherent material language); (C) "grounded remake" (the UE5
   fan look). The asset source follows from this choice.
2. **IP posture.** May New Dawn ship character or place art that depicts Nintendo's designs (commissioned,
   hand-made or AI-made), or only code plus content generated locally from the player's own dump (ND-002's current
   stance)? This is the deciding factor between plan D and everything else. Ideally ask a lawyer.
3. **Is an MM3D dump an acceptable prerequisite** for the better models (two dumps instead of one)?
4. **Scope of the texture pass.** Everything (a baseline for all 8,284 world and character textures), or the
   top-N by coverage with the rest left to the local baseline; and what first-run time and cache size you
   accept (my numbers: 2-8 min, 0.5-2 GB).
5. **Dependence on the community pack.** Is it acceptable that New Dawn's materials work *without* it (plan A),
   superseding the ND-002/ND-009 assumption that textures come from a community mod? Should we approach
   GhostlyDark/Nerrel for an explicit licence or permission?
6. **Budget and skills.** Is there money for commissions (kit of 15 models, roughly USD 15-60k) or hand-made art,
   and who authors the stylised library? Without a budget, plans A, B and E are the realistic ones.
7. **Engine investment.** Approve a renderer-side skinning prototype and object-path material maps regardless of
   the asset plan? Both help every model route.
8. **Validation set.** Accept the tier-0 kit in report section 1.4 (five Link forms, Tatl, Anju, Soldier, Happy
   Mask Salesman, Romani, Deku Baba, Octorok, Odolwa, pot, chest, sign) as the models we validate on, or name
   others.
9. **Order of work against the roadmap.** These tracks compete with the planned character materials and hair
   work on the ROADMAP; say which is first.

## Not decided here

The visual target, the IP posture, the budget and the scope are yours; none of the plans above has been
implemented. The proof-of-concept images are in `poc/` and are game-derived, so they stay outside the repository.
