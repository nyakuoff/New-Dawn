# Water polish (issue #84)

Visual-only fixes found in a manual playtest. Evidence: `~/dev/ghm-mods/showcase/fix-84/`.

## Rain puddles (`gfx_opengl_weather.cpp`, wet layer at OPAQUE_END)
Root cause of the "flat, pale" look: the old wet pass mixed the ground towards the sky cube by up to 70% on every flat texel
(a grey film with no shape). Now:
- **Shape**: world-anchored 3-octave noise plus a cavity term (four validated depth taps: is this texel lower than its
  surroundings) gated by surface flatness. The threshold falls as wetness rises, so puddles grow in the rain and shrink while
  the wetness lingers away. A wider damp surround is darker and more saturated.
- **Water**: dark cool bed under a film; reflection `bed*(1-F) + R*F`, `F = mix(0.16, 1, (1-N.V)^3.5)`, where `R` is a screen-space
  march of the opaque depth/colour (44 steps, binary refine, edge fade) falling back to the sky specular cube.
- **Ripples**: two analytic ring layers (gaussian-shell sine packets) perturb the normal and the CC0 ring sprite is scaled to the
  same ring as a bright crest. Only while raining; faded with distance.
- Cost: stage 12 0.13 -> 0.33 ms in Clock Town rain (RX 7800 XT). New uniforms `uViewProj`, `uNearFar` on the wet program.

## Water reflections (`kWaterShader`)
Root cause of the "mirror": a barely perturbed normal (`slope * 0.25`), a constant 1.6 px gather, and a `sqrt(F)` weight.
- Normal: glint slope plus two finer travelling octaves (17 and 6.5 units), footprint-faded.
- Blur: 12-tap gather whose radius grows with the distance between waterline and reflected point and with wave steepness,
  smeared 1.8x vertically, depth-validated (radius 0.65 - 9 px).
- Weight: `min(F^0.62, 0.85)` instead of `sqrt(F)`, times a slow break-up noise (up to -30%) and a distance fade (up to -45%).
  Sky-cube lookups are blurrier and never sample the lower hemisphere. Confident tower/pier hits stay readable.
- Cost: water stage +0.03 ms at the Coast.

## Splashes behind the water
Root causes:
1. The water surface pass (`RunWaterSurface`/`RunWater`) ran at SCENE_END, after every XLU effect. It rebuilt the pixel from the
   bed colour and kept only a high-pass of the old colour, so splash/ripple quads (no depth write) were mostly erased and the
   remnant added on top as extra brightness.
2. The splash textures (`gEffWaterSplash1-8`, `gEffWaterRipple`) are **not** caught by the VFX dust class (`gEffDust*`), checked
   with `gNewDawn.Vfx.Debug`. They are plain opaque-white native quads (prim 255,255,255,255), too bright in HDR.

Fix: new stage `NEWDAWN_POSTFX_STAGE_WATER_XLU` (14), emitted by `NewDawnPostFx_WaterXlu` right before `Effect_DrawAll` in
`Actor_DrawAll` (game code, one call). The water pass moved there, so `EffectSs` splashes/ripples/dust draw natively above the
finished surface. Underwater, VFX sprites, bloom and grade stay at SCENE_END. The sun-glare attenuation is no longer applied
(the glare fill is drawn after the marker). Brightness: two narrow VFX classes `VFX_WATER_SPLASH` (x0.5) and `VFX_WATER_RIPPLE`
(x0.8) only dim prim colour/alpha of those native draws (`mVfxDim`); they are neither recorded nor replaced, and only active
while the VFX recorder runs (HDR + `gNewDawn.Vfx.Enabled`).

## Verification
Clock Town day 2 rain (frozen frame 120), Great Bay Coast, Link wading (private fixture
`~/dev/ghm-mods/work/fix84/fixture.py --walk-at`), 0 GL errors (`MESA_DEBUG=context`). Off identity: frozen Stock Pot Inn
with water/atmos cvars off, main reference vs this branch x2 each, all SHA-256 `e458ebe3...`.

## Limits
- The "before" binary is an older main (`296cf87fd`), so its Coast shot lacks caustics; compare with `Water.Caustics=0`.
- Effects now draw after the water pass, so effects below the surface (underwater bubbles) are no longer absorbed by the restyle.
- Puddle reflections come from the opaque frame only (no translucent effects, no off-screen geometry).
