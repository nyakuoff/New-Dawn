# New Dawn — working state

> **Live handoff document.** Keep this brief and update after meaningful milestones. Record facts, not aspirations. Only one integration owner should edit this file at a time.

## Current milestone
**New Dawn menu and UI** (`feature/new-dawn-menu`, local, built and captured; the user's manual pass is next; see `docs/MENU.md`). Earlier: **Remaster roadmap, item 3: physical sky and volumetric clouds** (`feature/physical-sky`, in progress). In parallel: MM3D polish and the opt-in MM3D collision trial (`feature/mm3d-polish`).

## Implemented (branch exceptions noted below)
| Feature | Settings | Evidence |
| --- | --- | --- |
| Post-processing: SSAO, bloom, grade, dither | `gNewDawn.PostFx.*` | #7. Independent review; on/off captures. |
| Mipmaps + 16x anisotropic filtering | `gNewDawn.Textures.Mipmaps/Anisotropy/LodBias` | #10, #15 (the packaged shader sync made the seam fix active in-game). With the 4K pack, distant aliasing is down 34-38%. |
| Texture cache VRAM budget | `gNewDawn.Textures.CacheBudgetMB` (2048) | #14. A 64 MB stress test: no missing textures. |
| Async HD texture loading | `gNewDawn.Textures.AsyncAltLoad` | #16 and #19. The scene-entry stall drops from about 290 to about 175 ms. Mod-only textures load synchronously. |
| Per-pixel lighting (opt-in) | `gNewDawn.Lighting.PerPixel`, `.PointLightQuality` | #15. Off is pixel-identical; point lights were verified in the Astral Observatory. |
| Real-time sun shadows (opt-in) | `gNewDawn.Shadows.*` | #21. Ikana and Termina cast correctly; 0.3 ms GPU. Checked in motion (walking, interpolated sub-frames, time-of-day fades): no swimming or lag. In the preset (strength 0.7, floor 0.65 for soft daylight shade; `gNewDawn.Shadows.Floor` default 0.55); the cvar default is still off. |
| Cascaded sun shadows | `gNewDawn.Shadows.Cascades/Distance` | 4 cascades in a 2×2 atlas over the existing map, texel-snapped, blended, out to 7000 units; sharper near shadows; all consumers (apply, fog, shafts, GI, particles) use them. Cascades=1 is the old path (frozen Inn byte-identical). +0.0–0.18 ms. In the preset. See `docs/CASCADED_SHADOWS.md`. |
| Foliage shadows (opt-in, needs shadows) | `gNewDawn.Shadows.Foliage` | Texture-edge cutouts (fences, grass, leaves, Link's cut-out parts) are recorded with UVs, batched by texture, and drawn into the shadow map with a 0.19 alpha discard. +0.03 to 0.055 ms GPU in the shadow stage; in the preset. Off is the old path. |
| Sun shafts / rays | `gNewDawn.SunShafts.Enabled/Mode/Strength` | **Mode 1 (preset): screen-space crepuscular rays, 0.17-0.19 ms, see `docs/SUN_RAYS.md`.** Mode 0 is the shadow march below: | Half-res raymarch through the shadow map, tinted by fog and sun colour, in the preset. Shadow filter is now a rotated Poisson PCF. Off is pixel-identical; 0.25 ms GPU for shafts, shadows 0.25 ms (was 0.30). |
| Alpha-to-coverage for cutout textures (MSAA only) | `gNewDawn.Textures.AlphaToCoverage` (default 1) | Texture-edge draws keep the 0.19 threshold but resolve the edge through MSAA coverage; no effect at MSAA 1. Leaves, plants and ornaments are smoother; region mean colour changes under 0.2%. In the preset. |
| Water glints and Fresnel (opt-in) | `gNewDawn.Water.Enabled/Strength/Debug` | Scene-end pass over the collision WaterBox planes (nearest 16): sun/moon GGX glints from a footprint-filtered 2-octave scrolling normal, faint sky tint, shallows fade; per-scene factor table (poisoned swamp 0.3). 0.05 ms GPU. In the preset. |
| HD VFX replacement (fire, glows, embers, dust, sparks) | `gNewDawn.Vfx.Enabled/Strength/Embers` | `docs/VFX.md`. Native effect sprites are recorded by texture path and redrawn as HD HDR sprites (CC0 Kenney art, kept local); torches, light glows, embers, dust, hit/electric sparks, bomb fireball (verified with a thrown bomb); Inn candles/fireplace (`docs/VFX.md` extras). Off is byte-identical to main (frozen Inn); stage 14 0.004–0.007 ms. In the preset (`feature/vfx-overhaul`, local). |
| Water foam, refraction, restyle, sharper SSR | `gNewDawn.Water.Foam/Refraction/Restyle` (+ `FoamStrength/RefractionStrength`) | `docs/WATER_FOAM.md`. Depth-aware shore/contact foam, wave-normal refraction with Beer–Lambert absorption, per-biome water palettes (poison stays purple), finer SSR filter (the blurry-water fix). Water stage 0.333 → 0.476 ms; off is byte-identical to main (Stock Pot Inn, 4 repeat runs). In the preset. |
| Water caustics and underwater look | `gNewDawn.Water.Caustics/Underwater` (+ `CausticsStrength/UnderwaterStrength`) | `docs/WATER_CAUSTICS.md`. Self-rendered seamless caustics flipbook on lit beds/walls below (and faintly above) WaterBox planes, shadow-mapped; camera below a plane gets absorption/in-scatter in the area palette, half-res sun shafts, wavy refraction and floating particulates (atmos biome 6). Caustics 0.05–0.11 ms, underwater ~0.4 ms. In the preset. Zora Hall pool, Clock Town fountain and interior underwater unverified. |
| Linear HDR world and effects | `gNewDawn.HDR.*` | #30, #32. RGBA16F world and MSAA resolve; linear AO, shadows, shafts, water/SSR and bloom. HDR and MSAA 4 are in the preset. Evidence: `docs/HDR_M2.md`. |
| Hue-preserving tonemap and modern grade | `gNewDawn.HDR.*`, `gNewDawn.PostFx.Grade.*` | #38. This replaced the per-channel ACES fit and the double grade (saturation was +10–45% and shadows crushed). Identity below 0.76, then a highlight roll-off, plus an HDR split tone. Graded deliberately richer than the original: saturation -9% to +18%, clipping ≤ 0.72% over 12 points. |
| Material maps (normal and roughness, GGX) | `gNewDawn.Materials.*` | #31, #33, #34. An offline generator covers 2,209 world textures (the archive stays local). The specular wash is fixed. POM (`gNewDawn.Materials.Parallax`, depth `.ParallaxScale`, in the preset) gives brick, cobble and flagstone real depth; see tools/newdawn/README.md. Wood and plaster change little. Highlights stay restrained. |
| MM3D (3DS) room geometry, experimental | `gNewDawn.MM3D.Rooms/Path/Knee/Top/Gain` | #36, #37. A clean-room converter from the user's own dump; an OpenGL draw path keeps N64 collision. A data-driven allowlist of 19 scenes, with live N64 texture animation and a PICA highlight knee. Rooms=0 is pixel-identical to main; about 0.1 ms GPU. Not in the preset. |
| Character lighting (rim + bounded metal specular on actors) | `gNewDawn.Lighting.Characters/CharactersStrength` | `docs/CHARACTER_LIGHTING.md`. Render-state classifier (non-identity matrix, 2+ matrix loads after the actor's lights); +0.01 ms GPU; off is pixel-identical. In the preset. |
| Screen-space bounce light (SSGI) | `gNewDawn.GI.Enabled/Strength/Debug` | `docs/SSGI.md`. One-bounce screen-space diffuse gather from this frame's opaque colour, bilateral + history filter, additive composite; 0.56-0.88 ms; off byte-identical to main (frozen Inn). In the preset. Local branch `feature/ssgi`, not pushed. |
| Local lights and night contrast | `gNewDawn.Lighting.Local/LocalStrength/LocalContrast/LocalFlicker` | `docs/LOCAL_LIGHTS.md`. Screen-space HDR pass at opaque-end: game point lights + VFX flames, inverse-square, flicker, contact shadow; darkens unlit night/interior. 0.13-0.29 ms; off byte-identical to main (frozen Inn). In the preset. Local branch `feature/local-lights`, not pushed. |
| 3D grass (render-only tufts) | `gNewDawn.Grass.Enabled/Density/Distance/Height` | `docs/GRASS.md`. Grass-classified ground triangles are recorded, scattered in a world-stable hash grid, drawn as swaying alpha-tested tuft cards that take the ground's finished colour; 0.4–0.8 ms GPU in meadows; off is a no-op. In the preset. Local branch `feature/render-grass`. |
| New Dawn menu and looks | `mm/2s2h/BenGui/` (`NewDawnTheme`, `NewDawnPresets`, `Menu`, `BenMenu`), `gNewDawn.Setup.Version/Look` | `docs/MENU.md`. Pages and tabs over the existing widget registry; Ultra/High/Medium/Low/Original look from one table (`kRows`); Ultra applied on a fresh install, a one-time Apply/Keep popup for existing configs; asset mods are not part of a look. Local branch `feature/new-dawn-menu`, not pushed. Replaces the #17 preset buttons. |
| Night sky (stars, Milky Way, Moon glow, moonlight shadows) | `gNewDawn.Sky.Night/StarBrightness/MilkyWay/MoonGlow/Moonlight/MoonlightLift` | `docs/NIGHT_SKY.md`. 13,964 HYG catalogue stars as full-resolution point sprites (flux, B-V colour, extinction, horizon scintillation, cloud occlusion), a NASA SVS Milky Way panorama in the sky shader (so the reflection cube gets it), both rotating with the clock; Moon halo anchored to the En_Fall Moon actor; Moon as the shadow light at night with a faint cool lift; final day/Ikana/swamp hide the stars. Assets local in `~/dev/ghm-mods/assets/nightsky/` (HYG CC BY-SA 4.0, NASA SVS/Gaia). Stars 0.002 ms, sky +0.02 ms; off byte-identity not demonstrated (frozen Inn noise, same as Special Skies). In the preset. Local branch `feature/night-sky`, not pushed. |
| Capture tool, branding, docs | `tools/newdawn/`, `docs/images` | #8, #9, #11 |

Mods are installed locally and never committed: MM Reloaded 4K is loaded first, then 3DS Link and Equipment, then HD Moon. They are in `build-cmake/mm/mods`, and the capture sets are in `~/dev/ghm-mods/sets/{textures,full}`.

**Regression sweep 2026-11-12** (main `f9c4dff6c`):
- With everything off and no mods, all 12 points match `build-baseline` within its own run-to-run noise.
- With the preset and the full mods set: no magenta or missing textures, 0 null-texture and 0 GL errors in the logs. The image is slightly darker overall (AO plus contrast).

## In progress
- Glare/fog on `feature/glare-fog` (local only): E2 `1c93a00a9` / renderer `b8553b83`;
  E1 replacement renderer `30e63e37`. `gNewDawn.Sky.Flare` replaces sun sprites/wash with depth-occluded
  diffraction (~0.164 ms GPU); height fog is 0.305–0.351 ms over the requested static matrix.
  Both toggles remain default/preset off: exact-main frozen Inn byte identity is not established;
  Termina sun framing stopped after 3 failures; dramatic dusk fog shafts remain undemonstrated.
  See `docs/SUN_GLARE.md`, `docs/VOLUMETRIC_FOG.md`.
- Physical sky (A), then volumetric clouds (B): `gNewDawn.Sky.Physical/Clouds`.
- MM3D polish: floor banding, an on-screen check of re-admitted scenes, sunlit shadows, then the opt-in `gNewDawn.MM3D.Collision` trial on South Clock Town.

## Next priority
1. ~~Sky-driven ambient / IBL~~ (C2 and D1 done, `docs/SKY_AMBIENT.md`, `docs/SKY_REFLECTIONS.md`); D2 winter overcast sky (`docs/SKY_OVERCAST.md`) done. Special skies (final day, cursed Ikana, poisoned swamp; `gNewDawn.Sky.Special`, `docs/SPECIAL_SKIES.md`) done, on `feature/special-skies`.
2. Deeper shadows (alpha-tested foliage casters, cascades: done) plus SSGI and volumetric fog.
3. ~~Shoreline foam and refraction for water~~ (done, `docs/WATER_FOAM.md`).
4. Still owed: interactive HDR pause/pictograph, resize and scene-transition validation; motion and time-of-day checks of the #38 grade.

## Known limitations
- Dusk fog shafts and Coast SSR specks follow-up (`fix/fog-shafts-water-specks`, 2026-11-14): stopped at the three-attempt limit; renderer experiments reverted, documentation/evidence only. See `libultraship/docs/NEW_DAWN_FOLLOWUPS.md`. Dusk shafts are now delivered by the screen-space rays (`docs/SUN_RAYS.md`); the water specks were not reproducible.
- Volumetric fog (`docs/VOLUMETRIC_FOG.md`) is in the preset: morning haze and Woodfall Temple light fog are verified; distinct dusk sun shafts are **not** demonstrated (Field has too few casters near the low sun). Textured lens flare (`Sky.Flare`) is in the preset and was checked with Water Restyle at the dusk Coast (no double attenuation seen).
- Shadows: casters the game culls itself (room meshes wholly behind the camera or beyond zFar, distant actors) are missing; room meshes outside the FOV are recorded (see `docs/CASCADED_SHADOWS.md`). Cut-out foliage casts only when `gNewDawn.Shadows.Foliage` is on (texture-edge draws whose alpha is the tile-0 texel; xlu/decal/particles and masked or blended textures still do not cast).
- MM3D rooms: 14 animated scenes stay N64 (their animated surfaces aren't modelled in MM3D at the same place); remodelled outdoor scenes need MM3D collision first.
- Post-fx and mipmaps are OpenGL only. With MSAA, AO uses resolved depth; HDR AO and sun attenuation still approximate indirect/direct separation.
- With mipmaps on, vanilla textures use bilinear filtering instead of three-point. The N64 two-tile LOD is not emulated.

## Active blockers
- None.

## Session handoff
- Large glare/fog scratch: `~/dev/ghm-mods/work/glare-fog/`; old sky capture directories moved from tmpfs to `~/dev/ghm-mods/work/sky-archive/`.
- Settings note: the cvar prefix was renamed from `gGhm.` to `gNewDawn.`. Old config keys are simply ignored.
