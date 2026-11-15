# Night sky (roadmap item 6)

Real stars, a real Milky Way, a glow around the giant Moon and soft cool moonlight shadows. Visual only: every input
is read-only game state (clock, sun direction, weather, the En_Fall Moon actor's position and scale, the day). Nothing
is written back and no gameplay, timing, RNG, collision or animation state changes.

## Settings
| CVar | Default (preset) | What |
| --- | --- | --- |
| `gNewDawn.Sky.Night` | 0 (**1**) | Star catalogue, Milky Way, Moon glow. Needs Physical Sky and HDR, and the `nightsky` asset pack. |
| `gNewDawn.Sky.StarBrightness` | 1 | 0-8 gain of the point stars. |
| `gNewDawn.Sky.MilkyWay` | 1 | 0-4 gain of the Milky Way panorama (also in the reflection cube). |
| `gNewDawn.Sky.MoonGlow` | 1 | 0-4 gain of the Moon halo. |
| `gNewDawn.Sky.Moonlight` | 0 (**1**) | Moon shadows and a faint cool lift (needs Shadows and HDR). |
| `gNewDawn.Sky.MoonlightLift` | 1 | 0-3 scale of that cool lift (0 = shadows only). |

The menu has Night Sky, Star Brightness and Moonlight Shadows beside the other sky options. With `Sky.Night=0` and
`Sky.Moonlight=0` every code path is the previous one (the native sprite stars are drawn, no new pass runs, the new
shader uniforms are 0 so the sky and shadow shaders add exactly 0 and multiply by exactly 1).

## Assets (local, never committed)
`<gNewDawn.Assets.Path>/nightsky/` = `~/dev/ghm-mods/assets/nightsky/` (the build symlinks asset packs), with its own
`MANIFEST.md` and `LICENSE-HYG.txt`. `tools/newdawn/nightsky/prep_nightsky.py` bakes it from public data:

| File | Source | Licence |
| --- | --- | --- |
| `stars.bin` (13,964 stars, V <= 6.9: unit vector, magnitude, B-V colour) | HYG Database v4.1 (Hipparcos + Yale Bright Star + Gliese) | CC BY-SA 4.0 |
| `milkyway_eq.f16` (4096x2048 half-float RGB panorama, equatorial) | NASA/GSFC Scientific Visualization Studio 4851 "Deep Star Maps 2020" (Hipparcos-2, Tycho-2, Gaia DR2 ESA/Gaia/DPAC) | NASA media (credit requested); Gaia DR2 is CC BY-SA 3.0 IGO |

It is real imagery and real catalogue data, not procedural shapes. Bright catalogue stars are despiked out of the
panorama (so they are not drawn twice); the glow, dust lanes and unresolved faint-star grain stay.

## Rendering
- **Stars** (`kStarVertexShader`, stage 20, after the clouds): 13,964 point sprites at full resolution, a Gaussian
  PSF whose width grows for the brightest stars (flux 10^(-0.4 mag)), B-V colour (tempered, a slight temperature
  variety), atmospheric extinction and reddening towards the horizon, scintillation that grows towards the horizon
  (about 4% overhead, 40% at the horizon, renderer wall-clock), and occlusion by the volumetric clouds drawn just
  before (the half-resolution cloud alpha at the star). The PSF uses `gl_FragCoord`, not `gl_PointCoord` (which was
  constant on this Mesa driver: stars rendered as nothing). Below the horizon nothing is drawn; terrain covers the rest.
- **Milky Way** (in `kPhysicalSkyShader`, so the clouds cover it and the sky cube / reflections get it): the panorama
  rotated by the clock; a seam-safe `textureGrad` for the screen and a high mip for the 32^2 cube faces.
- **Rotation**: the game's sun moves in a plane through the origin (`sunPos = (-sin a, cos a, cos a / 6)`), so the sky
  turns about that plane's normal, by the same hour angle, one turn per game day. The celestial pole is therefore
  about 9.5 degrees above the world's -Z horizon. The sun sits at J2000 RA 6h, so the galactic centre culminates near
  midnight. `CelestialMatrix` (double precision) builds the world to J2000 rotation each frame from the clock.
- **Visibility**: smooth from the sun at 1.7 degrees below the horizon to 17 degrees (astronomical twilight), times
  the variant (below), times rain (down to 10%) and the winter overcast deck (20%); `Sky.Strength` scales it.
- **Special skies** (`gNewDawn.Sky.Special`): final day dims stars to `0.2 * (1 - mood)` (and the storm deck covers
  the rest); cursed Ikana, the poisoned swamp and the winter overcast get none. Their art direction is unchanged. The
  native sprite stars are skipped only on frames where the physical sky pass was emitted, so any scene that falls back
  to the native sky keeps its native stars.
- **Moon**: the glow is anchored to the visible giant Moon, not the sun's opposite. `NewDawnFindMoon` reads the En_Fall
  Moon actor (position and scale), so the halo grows with the day's scale and follows the Moon as it falls on the final
  day (the game moves the actor); the angular radius uses the model radius (about 2200 units, calibrated against a
  capture). The glow is subdued under the final-day storm (0.6 + 1.6 * progress, times 1 - 0.4 * mood) and covered by
  clouds. The disc itself is still the game's world draw (the HD Moon mod applies).
- **Moonlight**: when the sun is down and the Moon is up, the existing shadow system uses the Moon as its light
  (`shadowIsMoon`): a cool light colour, strength 0.8 * fade, no sun shafts, no volumetric-fog sun. Shadows fall
  away from the visible Moon, leaned to at most 53 degrees (a Moon straight overhead hid every shadow under its caster
  in Clock Town, a limit of the existing map at zenith; leaning it keeps the azimuth and gives longer, readable
  shadows). Lit surfaces get a faint cool lift (`MoonlightLift`, +0.7 * N.L at most) so silhouettes read without
  brightening the night: the mean luma of Clock Town's lower frame moved from 35.8 to 36.7.

## Validation (2026-11-15)
Linux OpenGL 4.6 / Mesa, RX 7800 XT, 1920x1080, MSAA 4, `capture.py --preset --mods ~/dev/ghm-mods/sets/full`,
`MESA_DEBUG=context`, stats on. Private fixture `~/dev/ghm-mods/work/night-sky/run.py` (time/day set in the disposable
process only, as in PHYSICAL_SKY.md). Base: main `fc8459f1a` + libultraship `82d59e05`. Evidence (every image looked at),
left = Night/Moonlight off, right = on, in `~/dev/ghm-mods/showcase/night-sky/`:
- `hero_field`, `hero_milky`, `field_up` (Termina Field looking up, 00:30 and 22:30 with the galactic core and dust lanes),
  `field_horizon`, `town_midnight`, `town_up` (Clock Town at midnight), `sea_a/b/c` (Great Bay Coast over the sea),
  `final_field`/`final_horizon` (day 3, Special on: the storm deck keeps its look, stars hidden), `moon_view` (the
  native giant Moon with its halo), `*_before_after.png`.
- `dusk_dawn_before_after.png`: 18:15, 19:00, 05:00, 05:30. Stars come in as the sun sinks past about -2 degrees and
  are fully in by about 17 degrees; they are gone by 05:30 (the sky is already bright).
- `crop_1to1_stars_*.png` are 1:1 crops (no scaling: crisp points, B-V colours from blue-white to orange, no
  banding); `crop_3x_milkyway_dustlanes.png` is a 3x nearest zoom; `crop_1to1_native_stars_field.png` the old sprites.
- `moonlight_shadows_clocktown_before_after.png`: Moonlight 0 / 1 (subtle by design: mean luma of the lower frame
  35.0 -> 37.0, shadows of Link and the guard, cool lift on the lit paving).
- 0 GL, shader, link or framebuffer errors in all 32 captures plus the Clock Town and identity runs; the assets
  load once at start (`NewDawnNightSky: loaded 13964 stars ...`).
- GPU (300-run means): stage 20 (stars) 0.001-0.002 ms; physical sky stage 6 0.163-0.174 off -> 0.181-0.191 on
  (+0.02 ms for the panorama taps; the same program renders the reflection cube faces); clouds unchanged
  (0.23-0.32 ms); total night sky about +0.02 ms.
- **Off identity: not byte-demonstrated.** Frozen Stock Pot Inn (frame 30, `gMatchRefreshRate=0`,
  `gInterpolationFPS=20`, the full off list) against the unmodified play build of main (`fc8459f1a`, libultraship
  `82d59e05`): two runs each. Reference vs reference differ in 70k pixels (max 7), mine vs mine in 50k (max 5), and
  reference vs mine in 28-88k (max 4-11; one cross pair has max 2 and no pixel above 2): within that noise,
  not identical. Same limit as `docs/SPECIAL_SKIES.md`. By construction the off path is the old one: no new pass,
  `uMilkyGain`/`uMoonGlowAmt` are 0 (adds exactly 0), `uMoonLift` is 0 (multiplies by exactly 1), native stars kept.

## Limits and notes
- The reflection cube carries the Milky Way and the Moon halo (it renders the same sky shader); at night the sky is
  dark, so the extra reflection on water is faint. Individual stars are not in the 32^2 cube, and the SSR path reflects
  the star pass only where it hits on-screen sky.
- Stars are the same for every scene and day (one turn per game day, no annual drift); the sky is fixed at the world's
  nominal latitude (the pole about 9.5 degrees up toward -Z). They are hidden when the physical sky pass is not
  emitted (interiors, dungeons, sandstorms, scripted cutscene skies in non-Special mode): those keep the native stars.
- The Moon glow needs an En_Fall Moon actor in the scene (Termina Field, Clock Town and the rooftop have one); other
  scenes keep no glow and their moonlight shadows aim opposite the sun.
- Moon shadows are leaned to 53 degrees at most (see Rendering). The cascade map with a light at zenith is a limit of
  the existing shadow system, not changed here.
- Moon phase: the game has no Moon phases. The Moon's state is its scale/position (day, final-day fall), which the glow
  follows. The final-night close-up of the falling Moon itself was seen on the rooftop only with its native look.
- Wall-clock scintillation changes every frame by design; it is the only animated element and is independent of game
  time (like the cloud drift). `Sky.Night` is not part of the frozen-fixture off list because it is off there.
