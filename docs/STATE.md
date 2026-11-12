# New Dawn — working state

> **Live handoff document.** Keep this brief and update after meaningful milestones. Record facts, not aspirations. Only one integration owner should edit this file at a time.

## Current milestone
**M4: Next visual features.** M3 (sun shadows, opt-in) was merged in #21.

## Implemented (merged to `main` / `new-dawn`)
| Feature | Settings | Evidence |
| --- | --- | --- |
| Post-processing: SSAO, bloom, grade, dither | `gNewDawn.PostFx.*` | #7. Independent review; on/off captures. |
| Mipmaps + 16x anisotropic filtering | `gNewDawn.Textures.Mipmaps/Anisotropy/LodBias` | #10, #15 (the packaged shader sync made the seam fix active in-game). With the 4K pack, distant aliasing is down 34-38%. |
| Texture cache VRAM budget | `gNewDawn.Textures.CacheBudgetMB` (2048) | #14. A 64 MB stress test: no missing textures. |
| Async HD texture loading | `gNewDawn.Textures.AsyncAltLoad` | #16 and #19. The scene-entry stall drops from about 290 to about 175 ms. Mod-only textures load synchronously. |
| Per-pixel lighting (opt-in) | `gNewDawn.Lighting.PerPixel`, `.PointLightQuality` | #15. Off is pixel-identical; point lights were verified in the Astral Observatory. |
| Real-time sun shadows (opt-in) | `gNewDawn.Shadows.*` | #21. Ikana and Termina cast correctly; 0.3 ms GPU. Checked in motion (walking, interpolated sub-frames, time-of-day fades): no swimming or lag. In the preset; the cvar default is still off. |
| Sun shafts (opt-in, needs shadows) | `gNewDawn.SunShafts.Enabled/Strength` | Half-res raymarch through the shadow map, tinted by fog and sun colour, in the preset. Shadow filter is now a rotated Poisson PCF. Off is pixel-identical; 0.25 ms GPU for shafts, shadows 0.25 ms (was 0.30). |
| Alpha-to-coverage for cutout textures (MSAA only) | `gNewDawn.Textures.AlphaToCoverage` (default 1) | Texture-edge draws keep the 0.19 threshold but resolve the edge through MSAA coverage; no effect at MSAA 1. Leaves, plants and ornaments are smoother; region mean colour changes under 0.2%. In the preset. |
| New Dawn graphics preset buttons | the `sNewDawnPresetCvars` table in BenMenu.cpp | #17 |
| Capture tool, branding, docs | `tools/newdawn/`, `docs/images` | #8, #9, #11 |

Mods are installed locally and never committed: MM Reloaded 4K is loaded first, then 3DS Link and Equipment, then HD Moon. They are in `build-cmake/mm/mods`, and the capture sets are in `~/dev/ghm-mods/sets/{textures,full}`.

**Regression sweep 2026-11-12** (main `f9c4dff6c`):
- With everything off and no mods, all 12 points match `build-baseline` within its own run-to-run noise.
- With the preset and the full mods set: no magenta or missing textures, 0 null-texture and 0 GL errors in the logs. The image is slightly darker overall (AO plus contrast).

## In progress
Nothing is running. OpenCode Go hit its weekly limit on 2026-11-12, and Codex was limited until 12:21 Africa/Casablanca that day.

## Next priority
1. Make shadows default-on (#4). Motion checks passed; scene-to-scene transitions are untested.
2. Sun shafts / god rays (post-fx, reusing the shadow map for occlusion).
3. Water and sky improvements, investigated before committing to them.

## Known limitations
- Shadows: casters the game culls itself (room meshes beyond camera distance or FOV, distant actors) are missing. Foliage with alpha cutouts does not cast.
- Post-fx and mipmaps are OpenGL only. With MSAA, AO uses one depth sample.
- With mipmaps on, vanilla textures use bilinear filtering instead of three-point. The N64 two-tile LOD is not emulated.

## Active blockers
- None.

## Session handoff
- Settings note: the cvar prefix was renamed from `gGhm.` to `gNewDawn.`. Old config keys are simply ignored.
