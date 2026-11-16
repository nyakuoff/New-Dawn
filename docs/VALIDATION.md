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

## Deterministic frozen frame (#78)
`capture.py --freeze N` gives a capture that is the same pixels on every run, with the whole New Dawn preset on and no
other cvar switched off (the old per-feature off-list is retired). It sets three debug cvars (and fixes interpolation at
20 FPS, `gMatchRefreshRate=0`, `gInterpolationFPS=20`):

- `gNewDawn.Debug.FreezeFrame=N`: the game logic stops at the start of play frame N (the developer frame-advance pause), so
  the screenshot always shows the same game state. Pick N above the scene fade-in (30 for interiors, 60 outdoors).
- `gNewDawn.Debug.FreezeTime=1`: stops the render clock. Every wall-clock feature (wind, clouds, flicker, VFX, sky, grass sway,
  weather, particles, GI update rate, the interpolation phase of the water time) reads `Fast::NewDawnClockSeconds()`, which is
  then 0; the TAA jitter keeps one phase; local-light smoothing takes the game's values as they are; world-GI voxelisation
  draws one triangle at a time (the shader keeps the first writer, which is a GPU race otherwise); the game's RNG is seeded
  with 1 instead of the time, so the random torch flicker and particles repeat.
- When the freeze lands, the renderer restarts its temporal accumulators (TAA, screen-space GI, world GI) from nothing, lets
  240 identical frames accumulate, then stops writing their history. The result no longer depends on the frame rate or on
  how long the process has run.

```
tools/newdawn/capture.py --bin-dir build/mm --preset --mods ~/dev/ghm-mods/sets/community \
    --points stock_pot_inn --freeze 30 --cvar gNewDawn.Debug.BootHour=12 --wait 25
```

Rules: use the same `--wait` (>= 20 s) and the same mods/cvars on both sides; the first run with a mod set also builds the
texture cache (about 5 minutes, see "Loading and lighting materials"), so warm it with one long `--wait 300` run first.
Verified 2026-11-16 (RX 7800 XT, Mesa, 1920x1080, preset + community mods): Stock Pot Inn 5 runs with the same SHA-256, Termina
Field 3 runs the same. Not yet deterministic: Great Bay Coast (and probably other scenes where the game camera keeps easing
while the game logic is paused: the main camera's eye moves about 0.02 units a second there), so use interiors, Termina Field
and other still scenes for identity checks. To compare against an older build that lacks these cvars, switch the features off
on both sides and compare the same frame.

## Review levels
- Small isolated art/utility change: focused tests and self-review.
- Renderer, model loader, shader pipeline, or shared asset change: focused tests **plus independent review** and multiple representative visual checks.
- Changes reaching gameplay, collision, animation control, save formats, or timing: stop and redesign as visual-only, or obtain explicit user approval to change scope.

## 3DS character models
A model may be set `reviewed` in `tools/newdawn/mm3d/chartable.py` only when `tools/newdawn/mm3d/refgate.py` passes for its row (3DS-faithful reference render against the converted model: silhouette IoU and per-texture delta E), and it has been compared with the N64 model in the same frame in game. See `docs/MM3D_CHARACTERS.md`, "Reference gate".
