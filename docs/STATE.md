# New Dawn — working state

> **Live handoff document.** Keep this brief and update after meaningful milestones. Record facts, not aspirations. Only one integration owner should edit this file at a time.

## Current milestone
**M2: Texture quality.** This covers installing the MM Reloaded 4K pack, mipmaps and anisotropic filtering (issue #2), and validating VRAM and stutter with the pack (issue #6).

## Implemented (merged to `main` / `new-dawn`)
| Feature | Where | Evidence |
| --- | --- | --- |
| Scene post-processing: SSAO after opaque, bloom + grade + dither before HUD (`gNewDawn.PostFx.*`, "New Dawn" group in Graphics) | libultraship `gfx_opengl_postfx.cpp`, `mm/2s2h/Enhancements/Graphics/NewDawnPostFx.cpp`, hooks in `z_play.c` | Independent static review with 7 findings; the significant ones are fixed. On/off screenshots of the Clock Town title sequence. PRs lus#1, #7. |
| Mipmaps + anisotropic filtering (`gNewDawn.Textures.*`) | libultraship `gfx_opengl.cpp`, `default.shader.glsl` | With the 4K pack, distant aliasing is down 34-38%, VRAM is up about 190 MiB, and off matches baseline within noise. PRs lus#2, #10. |
| Headless capture tool | `tools/newdawn/` | 12 warp points plus attract mode. PR #8. |
| Branding: logos, icons, window title "New Dawn" | `docs/images`, `mm/{linux,macosx,windows}`, `BenPort.cpp` | Built, and the icon is confirmed inside `2ship.o2r`. The in-game menu bar has not been viewed. PR #9. |
| Mods installed locally (never committed): MM Reloaded 4K (loaded first), 3DS Link and Equipment, HD Moon | `build-cmake/mm/mods` symlinks; capture sets in `~/dev/ghm-mods/sets/{textures,full}` | Alternate Assets is on in the dev config |

## In progress
| Owner | Task | Status |
| --- | --- | --- |
| — | 4K pack validation (#6): load time, VRAM, hitching, post-fx over dense textures | Next |

## Next priority
1. #6: validate the 4K pack.
2. Per-pixel lighting (#3), then real-time shadows (#4).
3. Run the full `docs/VALIDATION.md` regression matrix with all features on.

## Known limitations
- Post-fx and mipmaps are OpenGL only. With MSAA, AO uses one depth sample.
- With mipmaps on, vanilla textures use bilinear filtering instead of three-point. The N64 two-tile LOD is not emulated.

## Active blockers
- Shared Claude allowance: Paseo Claude agents and the lead hit the 5-hour limit on 2026-11-12.

## Session handoff
- Settings note: the cvar prefix was renamed from `gGhm.` to `gNewDawn.`. Old config keys are simply ignored.
