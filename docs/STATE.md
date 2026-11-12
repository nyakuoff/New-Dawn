# New Dawn — working state

> **Live handoff document.** Keep this brief and update after meaningful milestones. Record facts, not aspirations. Only one integration owner should edit this file at a time.

## Current milestone
**M3: Shadows (#4).** M2 (texture quality, lighting, preset) is done and merged.

## Implemented (merged to `main` / `new-dawn`)
| Feature | Settings | Evidence |
| --- | --- | --- |
| Post-processing: SSAO, bloom, grade, dither | `gNewDawn.PostFx.*` | #7. Independent review; on/off captures. |
| Mipmaps + 16x anisotropic filtering | `gNewDawn.Textures.Mipmaps/Anisotropy/LodBias` | #10, #15 (the packaged shader sync made the seam fix active in-game). With the 4K pack, distant aliasing is down 34-38%. |
| Texture cache VRAM budget | `gNewDawn.Textures.CacheBudgetMB` (2048) | #14. A 64 MB stress test: no missing textures. |
| Async HD texture loading | `gNewDawn.Textures.AsyncAltLoad` | #16 and #19. The scene-entry stall drops from about 290 to about 175 ms. Mod-only textures load synchronously. |
| Per-pixel lighting (opt-in) | `gNewDawn.Lighting.PerPixel`, `.PointLightQuality` | #15. Off is pixel-identical; point lights were verified in the Astral Observatory. |
| New Dawn graphics preset buttons | the `sNewDawnPresetCvars` table in BenMenu.cpp | #17 |
| Capture tool, branding, docs | `tools/newdawn/`, `docs/images` | #8, #9, #11 |

Mods are installed locally and never committed: MM Reloaded 4K is loaded first, then 3DS Link and Equipment, then HD Moon. They are in `build-cmake/mm/mods`, and the capture sets are in `~/dev/ghm-mods/sets/{textures,full}`.

**Regression sweep 2026-11-12** (main `f9c4dff6c`):
- With everything off and no mods, all 12 points match `build-baseline` within its own run-to-run noise.
- With the preset and the full mods set: no magenta or missing textures, 0 null-texture and 0 GL errors in the logs. The image is slightly darker overall (AO plus contrast).

## In progress
| Owner | Task | Where |
| --- | --- | --- |
| DeepSeek 5eb97e6a | Shadows v1 (#4): same-frame deferred sun shadow map. Reconstruction is fixed (exact inverse P_matrix). Remaining problem: world geometry barely casts, probably because alpha-compare draws are skipped, so it is moving to alpha-tested casters. | `../nd-lighting`, branch `feature/shadows` (both repos) |

## Next priority
1. Finish and merge shadows (#4), off by default until validated at midday in several scenes.
2. Candidates after that: sun shafts / god rays (post-fx), water, and making shadows default-on in the preset.

## Known limitations
- Post-fx and mipmaps are OpenGL only. With MSAA, AO uses one depth sample.
- With mipmaps on, vanilla textures use bilinear filtering instead of three-point. The N64 two-tile LOD is not emulated.

## Active blockers
- None.

## Session handoff
- Settings note: the cvar prefix was renamed from `gGhm.` to `gNewDawn.`. Old config keys are simply ignored.
