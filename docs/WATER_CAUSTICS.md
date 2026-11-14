# Water caustics and underwater rendering

`gNewDawn.Water.Caustics` and `gNewDawn.Water.Underwater` (default **0**, **1** in the New Dawn preset), strengths
`CausticsStrength` (0–2, default 1) and `UnderwaterStrength` (0–1). OpenGL only. Visual only: the collision WaterBox
planes, the camera and the sun are read, nothing is written. Base: main `5b986cedd`, libultraship `bb286b7b`.

## Caustics (`RunCaustics`, opaque end)
- Runs before the opaque colour capture, so Restyle's refraction and absorption of the bed include the caustics. Output is a
  gain blended as `dst * (1 + g)` (no scene copy). Skipped on sky, below 2 units of a plane (the water mesh itself), deeper
  than 700 units, and on pixels whose refracted sun ray misses the WaterBox rectangle (14-unit soft edge).
- Below a plane: the sun (refracted at the surface, n = 1.33) enters at W on the plane; the pattern is sampled at W. Sun
  visibility is the sun shadow map (cascades supported) at W and at the receiver, so piers and overhangs shade the bed.
  Surface normal comes from depth derivatives (smallest neighbour difference), so walls and rocks receive the pattern by facing.
  Fades with `exp(-depth/130)`, tinted by `exp(-pathLength * absorb * 0.8)` with the area's Restyle palette (red goes first),
  and by the sun's colour and fade. Native fog is removed from the gain.
- Above a plane (0–90 units): the plane's mirror reflection of the sun lights walls and rocks next to the water with the same
  pattern (subtle, `0.12 + 3 * Schlick`, fades with `exp(-height/40)`); the reflection point W must lie in the rectangle and be sunlit.
- Interiors: without a time-of-day sun the scene's first directional light is used (tilted towards the vertical); the shadow
  map is not used there.
- Pattern: `assets/newdawn/caustics/caustics_8x4.png` (see below), two layers at different scale, rotation and phase
  (mean preserved), 12 fps flipbook with frame blending, mip-filtered by pixel footprint.

## Underwater (`RunUnderwater`, scene end; camera below a WaterBox plane)
- Camera-in-water is derived from the WaterBox planes (eye inside the rectangle, 0–1500 units below the surface, nearest
  surface wins). The state is smoothed on the CPU (snaps to 1 on entry, falls 0.25 per game frame after leaving), so crossing the
  surface cross-fades instead of popping.
- Per pixel, over the part of the view ray inside the water: Beer–Lambert transmission with `sigma = absorb * 0.5 + 1/2600`
  (the palette of the scene, see Restyle), ambient in-scatter of the palette's scatter colour darkening with depth
  (`max(exp(-depth/260), 0.18)`, tinted by sun and sky), and wavy refraction (about 3 px of slow sinusoidal screen displacement).
- Soft sun shafts: a half-resolution ray march (16 steps, jittered, lod-blurred pattern) through the caustic pattern at the
  surface entry point of each sample, sun shadow map visibility, absorption with depth, a Henyey–Greenstein phase (g = 0.55).
  Upsampled linearly into the full-resolution pass.
- Particulates: the atmosphere particle system gets biome 6 while underwater (motes lit by the filtered sun plus water glow);
  it is drawn by the opaque pass and therefore absorbed by the underwater fog like the scene. Needs HDR.

## Asset
`caustics_8x4.png` is self-rendered (no third-party art, CC0): `tools/newdawn/caustics/gen_caustics.py` (numpy + Pillow,
seed 7) refracts a parallel ray bundle through a periodic 22-wave animated height field and splats the photon density with
wrap-around; seamless in space and time. 2048×1024 grey, 32 frames of 256², value = density / 4. It lives in
`~/dev/ghm-mods/assets/caustics/` (MANIFEST.md) and is loaded from `<gNewDawn.Assets.Path>/caustics/` onto texture unit 28
(2D array, dummy bound from program creation). A missing file logs a warning and skips both passes.

## Validation (Linux, Mesa radeonsi, RX 7800 XT, 1920×1080, MSAA 4, preset + full mods, `MESA_DEBUG=context`)
- Great Bay Coast noon shallows, wading view: `~/dev/ghm-mods/showcase/underwater/coast-{shore,wade}-{off,on,split}.png` and
  `-crop11.png` (1:1, off above / on below). Caustic network on the sand and shallow bed, fading with depth, with Restyle on.
- Great Bay seabed, camera 250 units below the surface (private fixture, Link's Y held under the surface by a disposable
  memory poke): `dive-{off,on,split}.png`, `dive-crop11.png`: fog and absorption in the
  area palette, soft shafts, caustics on the bed, specks, distant geometry still readable.
- Laundry Pool, Link swimming in it: `laundry2-{off,on,split}.png` — caustics visible on the green bed (the off frame is at a
  different moment of the in-game dialogue; the scene is not frozen).
- Surface crossing: `surface-crossing-burst.png` (10 frames, 0.5 s apart; Link released from the dive and swims up).
- GPU (`gNewDawn.PostFx.Debug.Stats=1`, Coast): caustics 0.05–0.11 ms, underwater 0.37–0.40 ms (the first full-resolution
  version cost 1.1 ms, which is why the shafts are half resolution). Total about 0.5 ms while submerged, 0.1 above.
- Zero GL errors in all captures; GLSL passes `glslangValidator` in all four variants (plain/cascades).
- Off identity: with both cvars 0 the new passes do not run; the only always-on change is that the water fill gate and
  atmosphere gate read two extra cvars. The frozen Stock Pot Inn fixture (frame 30, 20 FPS, GI/Wind/Particles/Weather/
  LocalFlicker/Embers 0) is **not** byte-deterministic on this machine even main vs main (mean 0.18, max 8 over 1920×1080,
  HDR on); ours vs main is inside that band (mean 0.03–0.18, max ≤ 6). Hashes differ run to run for both.

## Limits
- Zora Hall: the pass runs (0.06 ms) but the central pool is deep and dark at the sampled views; no visible bed. The dive fixture
  could not hold Link underwater there (floor collision), so the underwater look in interiors is unverified. The Clock Town
  fountain was not located/verified. Laundry Pool underwater (camera below the surface) not captured.
- Underwater is not applied while the game's screen transitions or fills are active (same gate as Restyle).
- The distortion applies to the whole scene including Link and actors; the HUD is unaffected. The half-resolution shafts are
  filtered linearly (no depth awareness), so a faint shaft halo can touch silhouettes; hatching from the jitter is visible only
  at 2× zoom.
- XLU effects without depth writes (bubbles, splashes) are attenuated by the distance of what is behind them.
- Caustics animate at game time (20 Hz steps with frame blending); no interpolation between game ticks.
