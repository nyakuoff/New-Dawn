# Special skies

`gNewDawn.Sky.Special` (default 0, in the New Dawn preset). Requires Physical Sky and HDR. Visual only; every input is
read-only game state (scene, skybox id, curse flag, day, `gSaveContext.save.time`). Off, the classification is exactly
the previous one.

All variants reuse the physical-sky and cloud programs (stages 6/7) and therefore also feed the C2 irradiance and D1
reflection cubes. No image assets are used: the skies are procedural (physical scattering + the volumetric cloud march),
so there is nothing to add to an asset pack.

| Variant | Where | Look |
| --- | --- | --- |
| Final day | day 3 in the 13 classified outdoor scenes plus the poisoned swamp | Low (0.9-2.0 km), fast (2x to 5x the normal wind, sheared and domain-warped) storm deck, dark bodies with lit edges and gaps. Glow = the game's own final-day fog hue (acid yellow-green) in the morning, turning red-orange (1.0, 0.26, 0.05) from mood 0.45 to 0.85. Darker and more turbulent with the clock. |
| Cursed Ikana | `SKYBOX_3` with the curse fully applied (`D_801F4E74 >= 1`): Ikana Canyon, Graveyard, Stone Tower, Stone Tower Temple (inverted) | Desaturated violet-grey thin overcast and haze, almost still (0.12x wind), a faint violet floor at night. No clear blue. |
| Poisoned swamp | `SCENE_20SICHITAI`, and Woodfall's exterior until its temple is cleared | Sickly green horizon under a purple deck, a little green glow on cloud bases; matches the poison water palette. The native vortex cloud is a world draw and stays. |

Final-day mood (0-1) from the clock: 0.40 at 06:00 rising smoothly to 0.65 at midnight, then linearly to 1.0 at 06:00
(the final hours). It drives deck coverage/darkness, wind, turbulence and the glow's redness.

## Plumbing
- `NewDawnSky_Emit` (NewDawnPostFx.cpp) classifies and fills `skyVariant`/`skyMood`; the cursed pass is emitted after
  the game's opaque fog fill in `Environment_DrawSkyboxFilters`, like the winter overcast (D2), so the fill stays as the
  fallback.
- **Cutscenes (Special on):** the physical sky stays active through cutscenes, conversations, `lightSettingOverride`,
  skybox config changes and filter fills. It only reads the fog colour, palette (`skyboxCtx.prim`, which the game
  interpolates), sun direction and clock, so it follows each script's own lighting and time jumps. Scripted filter
  fills/fades are drawn by the game after this pass and cover it exactly as they cover the native sky. Still native:
  `shouldDraw` (late sky), `skyboxDisabled`, `sunDisabled`, non-time `lightMode`, sandstorms, non-classified scenes.
  The Clock Tower rooftop (`SCENE_OKUJOU`) is classified in Special mode, so the Moon's approach gets the day-1/2 sky
  or the final-day storm by the clock. Day 3 on any layer is allowed. Without Special all of these gates are the
  previous ones.
- `ComputeSkyLook` (gfx_opengl_postfx.cpp) turns the variant into uniforms: haze/deck/glow colours, cloud slab
  base/thickness, wind multiplier/shear/turbulence, coverage. Normal and winter rows reproduce the previous values.
- The cube cache refreshes when the variant changes or the mood moves by 0.01.

## Validation
Linux OpenGL 4.6, RX 7800 XT, 1920x1080, MSAA 4, `capture.py --preset --mods ~/dev/ghm-mods/sets/full`,
`MESA_DEBUG=context`, stats on. Cutscene bursts (8 frames, 1.5 s apart, test-only entrances, before/after) are in
`~/dev/ghm-mods/showcase/skies/cutscenes/`, with `moon_finalnight_{before,after}.png`; no pops or flashes across the
start and end of the checked cutscenes. 22 matched captures (Special 0 vs 1): zero GL/shader/FBO errors.
- Evidence (every image inspected): `~/dev/ghm-mods/showcase/skies/*_split.png` (left before, right after) and
  `*_crop1to1.png` (800x420 sky crops at 1:1), final day at 09:00/15:00/03:00 in Termina Field and South Clock Town,
  Ikana day/night, swamp noon/dusk, Woodfall exterior. `cloud_motion_burst_finalhours.png`: four frames 1.5 s apart.
- GPU (300-run means): sky 0.17 ms, clouds 0.22 ms (Ikana, thin) to 0.84 ms (swamp dusk, low deck), cube 0.05 ms:
  at most about 1.1 ms for the sky, inside the 1.5 ms budget. Native fallbacks run none of these.
- Frozen Stock Pot Inn (off identity): **not demonstrated.** Recipe from AGENT-RULES (frame 30, `gMatchRefreshRate=0`,
  `gInterpolationFPS=20`, GI/Wind/Particles/Weather/LocalFlicker/Embers/Clouds/Dither off) against an exact-base build
  (`5b986cedd` + libultraship `bb286b7b`, `~/dev/ghm-mods/work/skies/ref-base`). Two repeats of the *reference* differ in
  27k pixels (a window patch, max 8); two repeats of the branch differ in 47k-207k pixels (max 18-22), and branch vs
  reference differ everywhere (677k pixels) because the freeze lands on a different game frame (Link's pose differs),
  so the hashes never match even within one build. Three attempts, then stopped. Special 0/1 do not touch the
  interior (no sky stage runs there, all new code is behind the cvar).

## Limits
- The native Moon, sun and stars are still world draws in front of the sky; dense storm clouds do not occlude them.
  The giant Moon is drawn correctly in front of the final-night storm deck on the Clock Tower rooftop. The white
  specks in daytime swamp shots are the Atmos ambient particles, not native stars (the native swamp sky shows none).
- The cloud march's threshold shows faint terracing in thin, bright cloud edges (Ikana day, Clock Town day 3).
- Not verified in motion: a doorway transition, rain/ash (the game has no day-3 weather and none was added). Cutscenes
  verified: South Clock Town arrival (day 1), Clock Tower rooftop with the Moon (day 1 morning and final night); the
  Lost Woods opening dungeon has no sky. Song of Time dawn, giants and Moon-crash cutscenes were not reached by the
  test fixture. No per-cutscene fallback was needed. Snowhead/Great Bay/Romani and other unlisted outdoor scenes
  keep the native sky; the winter Mountain Village keeps the D2 sky on days 1-2 only.
- Stone Tower Temple (inverted) uses the cursed sky in its sky windows; only the Stone Tower tower views were framed,
  and they showed no sky.
