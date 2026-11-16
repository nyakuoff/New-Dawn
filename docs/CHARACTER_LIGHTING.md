# Character lighting

`gNewDawn.Lighting.Characters` (default 0; on in the New Dawn preset) and `gNewDawn.Lighting.CharactersStrength`
(0..2, default 1). Requires HDR and per-pixel lighting. Menu: Lighting, "Character Lighting".

## What it does
Lit actor draws get two additive terms in `newDawnCharacter` (default.shader.glsl, applied after the HDR diffuse):
- **Rim**: Fresnel term on the world normal, tinted by the sky irradiance cube sampled along the reflected view
  direction (unit 12, as #43), or by the scene ambient where sky ambient is not active (interiors). Directional and
  point lights add to it only when they are behind the subject as seen from the camera. The rim multiplies the
  albedo, so dark texels do not glow.
- **Specular**: GGX from the same light set. Material class comes from the albedo only (bright neutral = metal,
  skin hues = none, cloth = faint sheen). The lobe is bounded per channel by the lit albedo, so it cannot lift dark
  texels. The effect fades out between 400 and 800 world units from the camera.

## Classification (render state only)
A draw is a character when HDR and per-pixel lighting are on, the modelview matrix is not the identity (room
meshes use it), and at least two modelview matrices were loaded since the last `G_MW_NUMLIGHT` (`Actor_Draw` binds
lights once, then a skeleton loads one matrix per limb). Props drawn as actors (stalls, Clock Tower, doors) load a
single matrix and stay on the original shading. The camera position is solved from P (view*projection); it has no
solution for the HUD's orthographic projection, which therefore stays off. The root limb of a skeleton loads only
one matrix and is not lit by this effect.

## Limitations
- Sun shadows are applied afterwards in screen space to the whole colour, so a shadowed actor's specular is dimmed
  by the shadow floor (0.65 in the preset), not removed.
- Materials: companion-map surfaces (`gNewDawn.Materials`) keep their own specular.
- OpenGL only; GLES keeps the existing path.

## Evidence (2026-11-13, RX 7800 XT, preset + full mods)
Same-frame splits (left half off, right half on, from a temporary debug split that is not committed) and 2x crops:
`~/dev/ghm-mods/showcase/characters/split/{noon,dusk}-{south_clock_town,termina_field,southern_swamp,stock_pot_inn}`.
Off (HDR on) and Characters=1 with HDR off are pixel-identical to the previous main on a frozen Stock Pot Inn
frame. 0 GL errors with `MESA_DEBUG=context`. World draw batches: Termina Field 0.458 -> 0.467 ms, South Clock
Town 0.594 -> 0.606 ms (about +0.01 ms).

## Soft shading and shiny metal (fix #82, 2026-11-14)
Low-poly actor meshes exposed hard facet edges in the rim and highlights, and metal items looked pale.
- **Soft normal.** Everything that is not metal (rim, wrap, cloth/skin sheen) reads from the world normal pulled halfway toward the
  view vector, with a wider, lower-gain rim (`pow(.., 1.5)`, gain 3.4 instead of `pow(.., 2)` x 5.5), a wrapped back-light term and
  broader lobes (roughness 0.72). Point-light specular on non-metal is halved. The hat and NPC arms lose their facet-edge glints.
- **Metal.** Bright neutral texels (luma 0.22..0.5, fading out above 0.7 so white cloth, eyes and socks stay matte) are metal:
  roughness 0.24 core plus a broad lobe, F0 tinted by the albedo's own hue (`0.92 * (albedo / max)^3`), a budget of 4.5x albedo
  instead of 0.93x, 50% less diffuse, and a reflection of the prefiltered sky cube (`uSkySpecular`, unit 13, the same cube the
  material maps use; its declaration moved into the per-pixel block, the unit was already bound to a dummy at program creation)
  lifted x10 because the cube holds radiance well below sunlit surfaces. Shield rim: +22..35% brighter with a visible gradient
  instead of a flat grey (Clock Town day/dusk, `showcase/fix-82/crop-sh16.png`, `crop-sct_day.png`).
- Off (`Characters=0`) is unchanged: the frozen Stock Pot Inn (clock 12, frame 30, the rules' off cvar list) has the same
  SHA-256 `6286c8a1...` with the old and the new shader (three runs; the first run of a session landed on another frame, as documented).

## Modern character lighting (ND-014 direction, 2026-11-16)
Maintainer direction: characters are lit the way a modern remake would light them, not the way the N64 or the 3DS did.
Warm key and cool sky fill at a daylight white balance, soft wrap with subsurface colour on skin and cloth, rim light (the
rest of this document), colours that stay rich and never clip, moody dusk and night.

**What was wrong (measured).** `gNewDawn.Characters.LightLog=1` logs the light that reaches an actor draw. Termina Field
noon: ambient (0.41, 0.37, 0.37) x fill 0.72, key (1.00, 0.96, 0.90) from straight above; Clock Town noon: ambient (0.43, 0.37,
0.24), key (1.00, 0.92, 0.87); Termina dusk: ambient (0.37, 0.28, 0.23), key (0.76, 0.57, 0.49) at 9 degrees above the
horizon; Stock Pot Inn night: ambient (0.39, 0.39, 0.39), key (0.51, 0.59, 0.24) (a lamp, 55% up), fill (0, 0.08, 0). The
scene's warm ambient and key, then the display grade (S-curve contrast 1.08, saturation 1.12, the art direction's foliage-green
boost) multiplied onto saturated textures drove the weakest channel of Link's tunic to 0 in 93% of its pixels at noon and
100% at dusk (green (42, 124, 1): acid green), and made skin yellow (hue 41, value 0.89). This happens to N64 and 3DS
characters alike (the N64 tunic was 33% / 95% clipped), so it is not a conversion error.

**What the pass does** (`libultraship` `default.shader.glsl` and the composite shader; all of it only for actor draws that the
character classifier above selects, so the world keeps its warm grade):
1. **Daylight white balance** (`gNewDawn.Characters.LightNeutral`, 0..1, default 0.5 in Ultra, High and Medium). The light
   that reaches the actor is split into ambient (cool), key (warm) and point lights (kept: torches and fires stay orange);
   ambient and key move that far towards a cool and a warm daylight white, keeping their brightness (Rec.709 luma) and their
   direction. Dusk and night keep their tint (the scene's own colour is only half removed), they just stop clipping.
2. **Soft wrap with subsurface colour** (`gNewDawn.Characters.LightWrap`, 0..1, default 0.35). The key's diffuse term wraps
   past the terminator; the wrapped part takes the albedo's own colour (`(albedo / luma)^0.6`), so skin glows warm at the edge
   of the light and cloth deepens instead of going grey.
3. **Highlight shoulder and channel floor** (`gNewDawn.Characters.LightShoulder`, 0..1, default 0.5). A per-channel
   roll-off above 0.55 before the tonemap, and a soft floor under the weakest channel (17% of the luma, 80% applied): a
   saturated texel keeps a trace of every channel, as scattering in cloth and skin does.
4. **Grade relief** (`gNewDawn.Characters.GradeRelief`, 0..1, default 0.8; needs Local Lights or TAA, which provide the
   character stencil). The composite pass drops the display grade's *boosts* on characters (a contrast or saturation above 1,
   the foliage-green boost, the art contrast); its reductions and its colour tint stay. In Clock Town, where the grade
   desaturates, a first version that removed the whole grade made skin too orange; the one-sided version does not.
5. `gNewDawn.Characters.LightDebug` (1 total, 2 ambient, 3 key and point lights, 4 applied) shows the light a character
   receives; `LightLog=1` prints it.

**Off identity.** With `LightNeutral`, `LightShoulder`, `LightWrap` and `GradeRelief` all 0 and `Lighting.Characters=1`, the
frozen Stock Pot Inn (clock 12, frame 30, the full deterministic-off list of the agent rules) has SHA-256 `0645e06a...` in
two runs of this branch and two runs of a build of the unmodified libultraship (`9dab5985`): identical. With the defaults the
hash is `6943434a...`. The Low and Original tiers carry 0 for all four. 0 GL errors in every capture. GPU cost was not
measured; the mask pass reuses the TAA actor mask when TAA runs and otherwise costs one depth/stencil blit and one
fullscreen draw.

**Result** (3DS Link, one frozen pose, per-class mean RGB of the tunic and the skin; `ref` = the maintainer's reference
image, tunic (50, 108, 33), skin (190, 158, 112)):

| condition | | tunic before | tunic after | skin before | skin after |
|---|---|---|---|---|---|
| Termina noon | 3DS | (42, 130, 1), 93% clipped | (58, 118, 13), 28% | (230, 192, 104) | (218, 175, 100) |
| Termina noon | N64 | (47, 97, 6), 33% | (58, 98, 17), 33% | (224, 192, 107) | (220, 186, 105) |
| Termina dusk | 3DS | (43, 105, 0), 100% | (60, 101, 7), 9% | (190, 120, 76) | (199, 138, 90) |
| Termina dusk | N64 | (36, 66, 1), 95% | (47, 70, 14), 1% | (199, 139, 91) | (199, 137, 90) |
| Stock Pot Inn night | 3DS | (25, 88, 16), 52% | (35, 88, 19), 4% | (172, 154, 107) | (171, 153, 108) |
| Stock Pot Inn night | N64 | (34, 64, 23), 7% | (39, 65, 27), 0% | (175, 158, 109) | (181, 161, 111) |

Images: `~/dev/ghm-mods/showcase/mm3d-characters/light/` (`link_before_after.png`, `npc_before_after.png`; the guard and Anju at
noon and dusk keep their own colours and do not go grey).

**Limits.** The tunic at noon is still more saturated than the reference image's (0.89 against 0.70) because the 3DS
texture itself is (0.91): no lighting model lowers that without a material change, which this pass does not make. The N64
tunic at noon is unchanged in clipping (33%): its texture's blue is 0. The channel floor and the shoulder act on lit actor
draws only; characters under material maps (`gNewDawn.Materials`) keep their own path.

## Character materials (ROADMAP item 2, 2026-11-16)
Per-material response for characters, N64 and 3DS, on top of the lighting pass above. Characters were one material: a lit
albedo plus a rim and a metal guess from the texel colour. Now each draw knows what its texture is.

**Classification** (render state only; it never touches a game structure). Every character draw carries a class for its first used
texture: 1 skin, 2 costume (a CMB atlas holding tunic, belt, boots and skin: split per texel in the shader), 3 hair, 4 eyes and
mouth, 5 metal, 6 cloth, 7 leather, 0 none.
- 3DS models: the converter writes the class into bits 9 to 12 of the converted texture's flags (`TEX_FLAG_NEWDAWN_CHARCLASS`),
  from the texture's CMB name (`_e00`, `_m00`, `_ev`, `_eb` eyes and mouth; "hair"; "skin"; any other lit material a costume
  atlas) and the `MATCLASS` overrides of the table row (Link's face and hair textures, the Goron's and Zora's gear).
- N64 models: the class comes from the texture's resource name, split into its camel-case words (`gLinkHumanBeltTex` is
  leather, `gAnju1SkinTex` skin, `gSoldierArmorPlate1Tex` metal; palettes, icons, flames, numbers, skyboxes are 0), and every
  untitled `object_link_*` texture is a costume atlas. The class is cached per resource and logged with
  `gNewDawn.Characters.LightLog=1`; `LightDebug=5` paints the classes.

**Response** (`newDawnCharMaterial` in `default.shader.glsl`, added to the lit radiance, bounded by the albedo like the specular):
- skin: scattered light that turns red-orange in a band at the terminator and glows through thin parts when backlit, a faint
  oily sheen, a warm sky fill at grazing angles; skin hues keep their matte specular;
- cloth: a velvet sheen that grows towards the silhouette and follows the key light;
- leather: a broad, low highlight tinted by the leather;
- metal: forced crisp metal in the existing metal path for class 5;
- hair: two anisotropic bands along the strands (Kajiya-Kay, the second tinted by the hair);
- relief from the albedo (`gNewDawn.Characters.Relief`): the surface gradient of the texture's luminance from screen-space
  derivatives, no tangents or generated maps, so it works for the 4x-upscaled 3DS textures and the N64 ones alike; strongest on
  cloth and leather, weakest on skin; it perturbs only the diffuse and sheen terms.

Cvars: `gNewDawn.Characters.Materials` (0..1, default 0.8; Ultra, High and Medium) and `gNewDawn.Characters.Relief` (0..1, default 0.6;
Ultra and High). `MaterialTerms` is a debug bit mask of the terms (1 relief, 2 skin, 4 cloth, 8 leather, 16 hair, 32 sky).

**Design note.** The request listed "generated normal and roughness detail for 3DS textures". This pass derives relief in the
shader from the converted albedo instead of generating map textures: the 3DS atlases are 128 px sources upscaled to 512 px
and the gradient of their luminance is the detail they have, a generated map would hold no more; it needs no archive, no
cache and no extra sampler unit. Generated maps remain an option for the unique N64 art if a better detail source exists.

**Colour discipline.** The reference gate (`refgate.py`) is unchanged by the pass: it reads the converted textures, and the
only change to them is the class bits in their flags; all 58 rows give the same result before and after (the 6 default-on rows
and the Link forms pass; the Dinolfos and the Poe balloon stay out). In game, with the deterministic-off list and a frozen frame,
the pass changes Link's boots, hair, belt and shield rim (mean delta E over his pixels 3.2, only 1,684 pixels move), the guard
(mean 0.6 over the whole frame), the receptionist and the shop gal; the tunic's green is not touched by it (cloth sheen is
bounded by the albedo's own colour).

**Verification limits.** Off identity (`Materials=0`, `Relief=0`, and the four light passes at 0) is true by construction (the
class stays 0, the function returns first, no flush happens) but could not be shown bit-identical: with other sessions' games
running, the frozen Inn and Termina Field fixtures were not reproducible, two runs of the unmodified main build differing in 1.7 M
pixels. The off/on pairs of the sheets come from runs whose neighbourhood is identical; the Cremia and Anju pairs are excluded
(Cremia's cow and bushes move between runs; Anju sat outside the Inn frame at the deterministic camera). GPU cost not measured.
Images: `~/dev/ghm-mods/showcase/mm3d-characters/materials/` (`character_materials_reference_converted_pass.png`: the 3DS
reference render, the converted model, and the in-game frame with the pass off and on, for Link, the guard, Anju, the receptionist
and the shop gal; `captains_hat_fit.png`).
