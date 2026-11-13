# New Dawn — working state

> **Live handoff document.** Keep this brief and update after meaningful milestones. Record facts, not aspirations. Only one integration owner should edit this file at a time.

## Current milestone
**Remaster roadmap, item 3: physical sky and volumetric clouds** (`feature/physical-sky`, in progress). In parallel: MM3D polish and the opt-in MM3D collision trial (`feature/mm3d-polish`).

## Implemented (branch exceptions noted below)
| Feature | Settings | Evidence |
| --- | --- | --- |
| Post-processing: SSAO, bloom, grade, dither | `gNewDawn.PostFx.*` | #7. Independent review; on/off captures. |
| Mipmaps + 16x anisotropic filtering | `gNewDawn.Textures.Mipmaps/Anisotropy/LodBias` | #10, #15 (the packaged shader sync made the seam fix active in-game). With the 4K pack, distant aliasing is down 34-38%. |
| Texture cache VRAM budget | `gNewDawn.Textures.CacheBudgetMB` (2048) | #14. A 64 MB stress test: no missing textures. |
| Async HD texture loading | `gNewDawn.Textures.AsyncAltLoad` | #16 and #19. The scene-entry stall drops from about 290 to about 175 ms. Mod-only textures load synchronously. |
| Per-pixel lighting (opt-in) | `gNewDawn.Lighting.PerPixel`, `.PointLightQuality` | #15. Off is pixel-identical; point lights were verified in the Astral Observatory. |
| Real-time sun shadows (opt-in) | `gNewDawn.Shadows.*` | #21. Ikana and Termina cast correctly; 0.3 ms GPU. Checked in motion (walking, interpolated sub-frames, time-of-day fades): no swimming or lag. In the preset (strength 0.7, floor 0.65 for soft daylight shade; `gNewDawn.Shadows.Floor` default 0.55); the cvar default is still off. |
| Sun shafts (opt-in, needs shadows) | `gNewDawn.SunShafts.Enabled/Strength` | Half-res raymarch through the shadow map, tinted by fog and sun colour, in the preset. Shadow filter is now a rotated Poisson PCF. Off is pixel-identical; 0.25 ms GPU for shafts, shadows 0.25 ms (was 0.30). |
| Alpha-to-coverage for cutout textures (MSAA only) | `gNewDawn.Textures.AlphaToCoverage` (default 1) | Texture-edge draws keep the 0.19 threshold but resolve the edge through MSAA coverage; no effect at MSAA 1. Leaves, plants and ornaments are smoother; region mean colour changes under 0.2%. In the preset. |
| Water glints and Fresnel (opt-in) | `gNewDawn.Water.Enabled/Strength/Debug` | Scene-end pass over the collision WaterBox planes (nearest 16): sun/moon GGX glints from a footprint-filtered 2-octave scrolling normal, faint sky tint, shallows fade; per-scene factor table (poisoned swamp 0.3). 0.05 ms GPU. In the preset. |
| Linear HDR world and effects | `gNewDawn.HDR.*` | #30, #32. RGBA16F world and MSAA resolve; linear AO, shadows, shafts, water/SSR and bloom. HDR and MSAA 4 are in the preset. Evidence: `docs/HDR_M2.md`. |
| Hue-preserving tonemap and modern grade | `gNewDawn.HDR.*`, `gNewDawn.PostFx.Grade.*` | #38. This replaced the per-channel ACES fit and the double grade (saturation was +10–45% and shadows crushed). Identity below 0.76, then a highlight roll-off, plus an HDR split tone. Graded deliberately richer than the original: saturation -9% to +18%, clipping ≤ 0.72% over 12 points. |
| Material maps (normal and roughness, GGX) | `gNewDawn.Materials.*` | #31, #33, #34. An offline generator covers 2,209 world textures (the archive stays local). The specular wash is fixed. The visible gain is small; strong highlights only on wet, metal and tile plus POM is future work. |
| MM3D (3DS) room geometry, experimental | `gNewDawn.MM3D.Rooms/Path/Knee/Top/Gain` | #36, #37. A clean-room converter from the user's own dump; an OpenGL draw path keeps N64 collision. A data-driven allowlist of 19 scenes, with live N64 texture animation and a PICA highlight knee. Rooms=0 is pixel-identical to main; about 0.1 ms GPU. Not in the preset. |
| New Dawn graphics preset buttons | the `sNewDawnPresetCvars` table in BenMenu.cpp | #17 |
| Capture tool, branding, docs | `tools/newdawn/`, `docs/images` | #8, #9, #11 |

Mods are installed locally and never committed: MM Reloaded 4K is loaded first, then 3DS Link and Equipment, then HD Moon. They are in `build-cmake/mm/mods`, and the capture sets are in `~/dev/ghm-mods/sets/{textures,full}`.

**Regression sweep 2026-11-12** (main `f9c4dff6c`):
- With everything off and no mods, all 12 points match `build-baseline` within its own run-to-run noise.
- With the preset and the full mods set: no magenta or missing textures, 0 null-texture and 0 GL errors in the logs. The image is slightly darker overall (AO plus contrast).

## In progress
- Physical sky (A), then volumetric clouds (B): `gNewDawn.Sky.Physical/Clouds`.
- MM3D polish: floor banding, an on-screen check of re-admitted scenes, sunlit shadows, then the opt-in `gNewDawn.MM3D.Collision` trial on South Clock Town.

## Next priority
1. Sky-driven ambient / IBL, after the physical sky.
2. Deeper shadows (alpha-tested foliage casters, cascades) plus SSGI and volumetric fog.
3. Character rim and specular lighting; shoreline foam and refraction for water.
4. Still owed: interactive HDR pause/pictograph, resize and scene-transition validation; motion and time-of-day checks of the #38 grade.

## Known limitations
- Shadows: casters the game culls itself (room meshes beyond camera distance or FOV, distant actors) are missing. Foliage with alpha cutouts does not cast.
- MM3D rooms: 14 animated scenes stay N64 (their animated surfaces aren't modelled in MM3D at the same place); remodelled outdoor scenes need MM3D collision first.
- Post-fx and mipmaps are OpenGL only. With MSAA, AO uses resolved depth; HDR AO and sun attenuation still approximate indirect/direct separation.
- With mipmaps on, vanilla textures use bilinear filtering instead of three-point. The N64 two-tile LOD is not emulated.

## Active blockers
- None.

## Session handoff
- Settings note: the cvar prefix was renamed from `gGhm.` to `gNewDawn.`. Old config keys are simply ignored.
