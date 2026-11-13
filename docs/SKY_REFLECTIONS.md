# Sky reflections (D1)

`gNewDawn.Sky.Reflections` (default off; in the New Dawn preset). Requires Physical Sky and HDR. Visual only.

## Implementation
- Specular cube: 32² per face, 5 GGX roughness mips, 32 samples. It reuses the C2 8-frame sky cube cache and sits on
  dedicated sampler unit 13 (`uSkySpecular`). The unit is assigned at program creation with a neutral dummy cube
  bound, so HDR-off/Reflections-off programs never observe unit 0. NaN/Inf are sanitised at every cube write. The
  earlier black patches came from cloud marching on downward rays; they now return transparent.
- Water (`gfx_opengl_postfx.cpp`): premultiplied separate weights.
  `wSSR = strength·sqrt(F)·shore·confidence` (unchanged for confident tower/pier hits);
  `wSky = 0.4·strength·F·shore·(1-confidence)` with Schlick F0 = 0.02 and the per-pixel wave normal.
  A miss never raises strength. The 0.4 factor keeps near/mid water as turquoise as without reflections.
- Materials (`default.shader.glsl`, both copies identical): analytic split-sum, inside the existing per-channel/albedo
  budget × smoothness², so it can never lift dark albedo (the #34 wash).
- Scene classification is unchanged: Coast, Field and cleared Swamp use the cube; others keep the previous fallback.

## Validation (Linux OpenGL 4.6/Mesa, 1920×1080, MSAA 4, preset + full mods, `MESA_DEBUG=context`)
- Coast noon water saturation vs matched no-reflection shot: near 102%, mid 100%, far 105% (gate ≥90%); the tower
  SSR reflection is intact. (Previous iteration at full F weight: 90/78/70%.)
- Dusk Coast, Field pond and cleared Swamp: sunset glow and reflected clouds visible on the water.
- Turning burst (8 frames): no shimmer at 1:1.
- GPU: sky cube stage 0.046 → 0.068 ms; water stage 0.189 ms unchanged; all other stages unchanged (limit +0.15 ms).
  Material shader cost is part of world draws and is not separately measured.
- Zero GL errors in all captures. Stock Pot Inn frozen: Reflections 0 and 1 are byte-identical to main with HDR on;
  HDR off reproduces main's frame (the fixture alternates between two timing states, main does too).
- Materials: South Clock Town noon, mean luma ratio 0.9994, dark (<40) pixels change -0.09 code values, p99 pixel
  difference 2/255. Glossy tiles gain a faint sky sheen only, no greying.

Evidence: `~/dev/ghm-mods/showcase/sky-reflections/d1/{noon-v4,dusk-v4}/` (left half off, right half on),
`/tmp/nd/reflections/d1-v4-*`.

Limits: the cube is a low-resolution sky/cloud environment, with no local geometry reflection except SSR on water.
Interior and unclassified scenes are unaffected.
