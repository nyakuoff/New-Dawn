# Screen-space sun rays

`gNewDawn.SunShafts.Mode = 1` (preset default; `0` = the older shadow-map march) draws GPU Gems 3 ch. 13 style
crepuscular rays. It reuses the textured flare's sun projection, depth-occluded visibility and cloud opacity, so it
needs HDR and the Physical Sky at full strength (`Sky.Strength` 1). Strength is `gNewDawn.SunShafts.Strength`
(preset 0.30); it rises toward dawn/dusk and fades with sun elevation.

Pipeline (`PostFxGL::RunScreenShafts`, composited before the tonemap; the art grade runs after it):
1. Half-res sky mask: a pixel is "sky" where depth is at the far plane **and** the HDR scene matches this frame's
   rendered sky/cloud radiance (ratio 0.65-0.95). The colour test catches the game's depth-less conifer leaves,
   which a depth-only mask brightens. Weighted by a Gaussian around the sun's screen position.
2. 48-tap radial integration toward the sun with exponential decay and a spatially fixed interleaved-gradient
   jitter (no history, so nothing flickers when the camera moves).
3. Additive HDR composite, tinted by the sun light colour (75%) and fog colour (25%), masked by sky coverage so
   foreground silhouettes stay crisp, and faded by the flare's visibility and screen-border distance.

Gated off for night/no-sun, indoors/no skybox, sandstorm, underwater, lightSettingOverride, filtered skybox.

Evidence (1920x1080, RX 7800 XT, preset + full mods, `MESA_DEBUG=context`, GL error check on, strength 0.30):
- Cost **0.17-0.19 ms** GPU (stats stage "sun shafts"); the pass does not run (no stats) at night or looking away.
- Termina Field 17:30, clear sky: soft rays radiating through the conifer gaps, dark silhouettes. Thick dusk
  cloud over the sun (preset clouds) and Great Bay Coast with clouds: rays fade almost to nothing (cloud gate).
  Coast clear sky and cleared Swamp: soft, not overdone.
- Camera-rotation burst (test-only camera yaw 12 deg/s, 8 frames 0.35 s apart): rays fade smoothly as the sun
  leaves the frame, no flicker. 1:1 crop, plus a contrast-stretched copy: no banding.
- Off identity: frozen Stock Pot Inn (HDR and LDR, two runs each) is byte-identical between main and this build
  under the same preset (`docs/WATER_FOAM.md` fixture).
- Local images: `~/dev/ghm-mods/showcase/followups/rays/` (not committed).

Limits: the effect is screen space, so it cannot place fog in front of silhouettes and needs sky visible
behind the occluders; Woods of Mystery and Woodfall interiors show no sky in the tested views and stay unchanged.
Night moon variant was not built. Per-frame run-to-run differences in exterior on/off diffs are atmosphere motes
and dither, not the rays. Not tested: Great Bay open sea at an alternate sun azimuth, other time-of-day states.
