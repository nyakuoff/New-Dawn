# New Dawn — visual and regression validation

## Baseline first
Before each significant graphics change, record:
- Git commit/branch and relevant visual settings
- Hardware, graphics backend, resolution, frame cap, and target scene
- Equivalent camera framing and in-game time-of-day state
- Screenshot(s), and FPS/frame-time or memory measurements **only if tools provide reliable readings**

Store image comparisons in an appropriate project asset/test folder selected by the team. Do not commit copyrighted game imagery without confirming the project's redistribution requirements.

## Minimum change checklist
- [ ] Changed files and purpose are clear in the diff.
- [ ] Focused build, format, and available tests pass; note commands and any skips.
- [ ] Visual improvement is actually observable in a running game, or is explicitly marked **not visually verified**.
- [ ] No intentional changes to combat, movement, physics, AI, timing, interactions, NPC schedules, quests, or saves.
- [ ] Existing animations and skeletal compatibility are preserved.
- [ ] Collisions, triggers, and gameplay-critical geometry remain aligned with visuals.
- [ ] Performance compared under like-for-like conditions when the feature can affect GPU/CPU/memory use.
- [ ] No new proprietary game assets, ROM dumps, keys, or incompatible third-party licenses introduced.

## Representative regression matrix
| Area | Visual concern | Gameplay invariant |
| --- | --- | --- |
| Clock Town, day | Main environment / lighting / UI | NPC schedules and scene transitions |
| Clock Town, night | Fog / light source consistency | Clock / time progression |
| Termina Field | Distance / environment / sky | Movement and boundaries |
| One interior | Materials / local lights | Door and interaction behavior |
| One temple or dungeon room | Effects / shadow / readability | Puzzle and trigger behavior |
| Mask transformations | Character model / shader | Original skeleton and animation timing |
| Cutscene and dialogue | UI / post-process | Camera sequence and dialogue unchanged |

Do not mark the full matrix as passed after testing just one scene. For risky shared renderer changes, check more than one environment.

## Build and test commands
Verified on Linux (CachyOS, RX 7800 XT, OpenGL 4.6), 2026-11-12:
- Build: `cmake --build build-cmake`. This uses Ninja and RelWithDebInfo; the binary is `build-cmake/mm/2s2h.elf`. After a change to `mm/assets/custom`, also run `cmake --build build-cmake --target Generate2ShipOtr` to repack `2ship.o2r`.
- Frozen "before" build: `build-baseline/`, which holds the binary, `2ship.o2r` and `mm.o2r`.
- Captures: `tools/newdawn/capture.py` (see its README). It captures 12 warp points plus the title sequence, headless under gamescope. Use `--cvar` to toggle settings, `--mods ~/dev/ghm-mods/sets/{textures,full}` to load the mods, and `--compare A B` for side-by-side images. HD textures also need `--cvar gEnhancements.Mods.AlternateAssets=1`.
- Run-to-run capture noise is about 0.020 normalised RMSE (ImageMagick `compare -metric RMSE`). Treat a difference within that as unchanged.

## Review levels
- Small isolated art/utility change: focused tests and self-review.
- Renderer, model loader, shader pipeline, or shared asset change: focused tests **plus independent review** and multiple representative visual checks.
- Changes reaching gameplay, collision, animation control, save formats, or timing: stop and redesign as visual-only, or obtain explicit user approval to change scope.
