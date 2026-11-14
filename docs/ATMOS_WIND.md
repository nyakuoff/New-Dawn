# Wind and foliage sway (A3)

`gNewDawn.Atmos.Wind` defaults to **0**, is **1 in the New Dawn preset**, and requires HDR. `gNewDawn.Atmos.WindScale`
(default 1, clamped 0–10) is a foliage displacement multiplier for debugging.

## Rendering

- `libultraship/include/fast/newdawn_wind.h` supplies a monotonic clock, a fixed 2:1 horizontal
  direction, a 0.3–1.6 gust and its analytic time integral. Clouds, ambient particles, rain and
  snow use that integral for smooth travel; the rain streak direction uses the corresponding
  instantaneous velocity. Clouds sample density with the opposite offset sign so their visible
  motion follows the same world direction. With Wind off, clouds and ambient particles keep
  their previous drift and clocks.
- Room/scene geometry is bracketed by `NewDawnHDR_WindScope` in `Room_Draw`. The same markers
  bracket only the original draw of whitelisted foliage actors: EnWood02, EnKusa, EnKusa2,
  ObjGrass, ObjTree, ObjYasi, EnSnowwd and ObjHana. Actor state, update functions, collision,
  gameplay RNG and animation data are unchanged. Link and other actors are outside these scopes.
- Inside a scope the interpreter copies cut-out (`CVG_X_ALPHA`) triangle vertices. Texture-v
  weighting keeps bases planted. It displaces world and clip positions consistently; the
  foliage-shadow recorder consumes the same copy. Loaded vertices remain unchanged.
- Wind is sampled once per rendered frame; markers do not flush draw batches. This avoids
  per-triangle clock reads and per-actor batch breaks. No shaders or samplers are added for sway.
- `gNewDawn.PostFx.Debug.Stats=1` also prints displacement CPU milliseconds and triangle count
  per frame, averaged over 300 frames. This includes per-triangle timing overhead; it excludes
  marker generation, additional world-position calculations and shadow/GPU work.

## Earlier room evidence (2026-11-13)

Woodfall exterior plants, reeds and their water reflections move at scale 1 (a few pixels),
with an exaggerated scale-10 diagnostic and foliage/reflection diff mask. Evidence:
`~/dev/ghm-mods/showcase/atmos/a3/`. This earlier capture set predates actor scopes and shared drift.

## Integration validation (2026-11-14)

The exact-main disabled comparison now passes: Stock Pot Inn at 12:00, game logic frozen at
frame 30, interpolation fixed at 20 FPS (`gMatchRefreshRate=0`, `gInterpolationFPS=20`).
An isolated rebuild of main `f7f8b1f1c` / renderer `e4aba70e` and the atmosphere build use the
same archives, preset and full mods. Two repeats of each build (four images) have identical
PNG SHA-256 `46ad33f92c94c188f017961d297107a7295ca957e83e1ce39ec54ea04103329d`.
Raw evidence: `~/dev/ghm-mods/work/atmos/final-identity-*/`.

The analytic drift's numerical derivative matches gust speed to 0.00000015 in a private C++
check covering 0–1,000,000 seconds, including the previous discontinuous wrap boundaries.
The weather shader pair passes `glslangValidator`. Build commands remain `cmake --build build -j6`
and `cmake --build build --target Generate2ShipOtr -j6`; default shader files are untouched.

Termina ObjGrass tufts and Woods of Mystery EnKusa bushes visibly sway in four-frame bursts at
**WindScale 1**. Unscaled crops show a few pixels of leaf motion with planted bases. Separate-run native
plant orientation can vary; the frozen within-run bursts isolate the added sway. Off/on and
within-burst masks (`max(abs(RGB difference)) > 6/255`) isolate foliage and its shadow changes;
Link, HUD and nearby architecture stay fixed. With shadows disabled, the changing Woods ground
shadow disappears while the bushes still move. A fixed ground rectangle (x 940–1100, y 640–860)
has zero pixels over that threshold in the no-shadow burst.

CPU displacement means: **0.0223 ms/frame** in the close Termina view (172 triangles/frame),
**0.0072 ms/frame** in Woods (57 triangles/frame), **0.0105 ms/frame** in night Clock Town
(84 triangles/frame). Each is a 300-frame window with timer overhead included; these are not
measurements of total frame time. The earlier default Termina view measured 0.0257 ms.

Evidence (before/after, four-frame sheets, 1:1 crops and full-frame masks):
`~/dev/ghm-mods/showcase/atmos/final/{grass*,woods*}`. Private reproducible fixture and command
JSON files: `~/dev/ghm-mods/work/atmos/{final_fixture.py,run_final.py,final-*.command.json}`.
The fixture calls the canonical `tools/newdawn/capture.py` launcher with `--preset --mods
/home/loganp/dev/ghm-mods/sets/full`. It changes only disposable test processes.

A four-frame shared-wind burst shows cloud drift, motes and foliage together; an eight-frame
C-Up turning burst has no obvious wrap seams or flashes at the sampled views. Snow remains
soft and visible in winter Mountain Village. Night rain and rain-stop wetness validation are
recorded in `ATMOS_WEATHER.md`.

The rebuilt menu preset was captured without atmosphere overrides: all three effects are
selected by `sNewDawnPresetCvars`. A further off capture from that executable still has the
same exact-main hash. Final logs contain **0 GL/Mesa, shader or framebuffer errors** with
`MESA_DEBUG=context`. Raw stage measurements and image hashes: `final-results.json` in the
scratch directory above. The final integration touched only this game source and docs:
`mm/src/code/z_actor.c`, `mm/2s2h/BenGui/BenMenu.cpp`, `docs/ATMOS_{PARTICLES,WEATHER,WIND}.md`;
renderer: `include/fast/{interpreter.h,newdawn_wind.h}`, `src/fast/interpreter.cpp`,
`src/fast/backends/gfx_opengl_{atmos,postfx,weather}.cpp`. No new assets or dependencies.

## Limits

Texture-v weighting assumes tips occupy the top of the cut-out texture; unusual UV layouts
may sway imperfectly. Opaque trunks remain rigid. This is additive render deformation using
existing models and animations; collision remains at its original position. Camera bursts
are coarse motion evidence, not a continuous high-frame-rate or full gameplay regression test.
