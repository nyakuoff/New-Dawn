# Physical sky and clouds

## Milestone A: physical sky

The normal outdoor HDR sky now has opt-in Rayleigh/Mie single scattering in a spherical atmosphere. It uses the
same read-only directional light as shadows, the exact interpolated camera projection, and linear fog radiance at
the horizon. The half-resolution pass is reconstructed with bilinear filtering before native celestial/world draws.
No depth, game-clock, weather, collision, animation or gameplay state is written. The implementation and noise-free
scattering shader are original code; no image assets or external dependencies are added.

- `gNewDawn.Sky.Physical`: default **0**, New Dawn preset **1**; requires `gNewDawn.HDR.Enabled`.
- `gNewDawn.Sky.Strength`: default/preset **1**, range 0–1. Zero skips the marker entirely.
- Menu: Physical Sky (HDR), Sky Strength, beside HDR exposure.
- `gNewDawn.PostFx.Debug.Stats=1`: stage **6**, physical sky, plus occasional read-only scene classification logs.

Only explicitly classified outdoor scenes with normal skies/time-driven lighting are eligible. Unknown scenes,
interiors/dungeons, cutscenes, lighting overrides, scripted sky transitions, poisoned swamp, cursed Ikana (`SKYBOX_3`),
and the entire final day fall back. Winter Mountain Village uses `SKYBOX_2`, an empty sky replaced by an opaque
fog-colour fill; that special snow background also stays native. Cleared/spring versions can use normal skies.
This milestone does **not** claim that every outdoor sky has been replaced.

### Validation (2026-11-13)

Base: main `293b29f75`; libultraship base trees `8ee36d09` / `72833a6d` are identical. Linux OpenGL 4.6,
RX 7800 XT, 1920×1080, internal resolution 1, MSAA 4, the correctly typed showcase preset, full local mods.
Verified commands in this worktree:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j6
cmake --build build --target Generate2ShipOtr -j6
```

Both new GLSL programs also pass `glslangValidator` (GLSL 330). The two default shader asset copies are unchanged
and have identical text. Build and whitespace checks pass. Lead reviewed both repository diffs and approved the
structure; requested extra coverage is recorded below.

Local evidence (game imagery is not committed):

- `/tmp/nd/sky/physical-{morning,noon,dusk,night}/`: Termina Field, South Clock Town, Great Bay Coast, Ikana
  Canyon, winter Mountain Village at 08:00, 12:00, 17:30 and 22:00, all visually inspected. Normal skies have a
  continuous gradient, fog joins the horizon, stars remain visible at night, geometry remains present. Ikana and
  snow retain their distinct skies. No seams observed in these static views.
- `/tmp/nd/sky/physical-final/`: day 3, 23:00, Field/Clock Town; native ominous sky preserved, no sky GPU stage.
- `/tmp/nd/sky/physical-rain/`: day 2 noon, Field and poisoned swamp. Swamp has 40 visible rain drops and stays
  native. This Field point remains dry; **the active scattering/weather blend in rain still needs coverage**.
- `/tmp/nd/sky/{main,off}-noon/`: exact-base executable rebuilt from original source versus sky-disabled build,
  Field, Stock Pot Inn, Woodfall Temple and Ikana. Animation/noise prevent whole-frame byte identity outdoors.
- `/tmp/nd/sky/{main,off}-frozen/`: existing developer frame-advance at game frame 120 removes animation timing.
  Stock Pot Inn is **byte-identical**; Field RMSE **0.000987** (normalised), far below documented 0.020 capture
  variation. Most residual differences are one code value; render-frame-dependent N64 noise remains. Thus exact
  outdoor byte identity is **not proven**. With sky off no new marker or rendering pass executes.
- `/tmp/nd/sky/main-reference/`: disposable exact-base executable used for that comparison.

The private `/tmp/nd/sky/run.py` fixture selects time/day in each disposable capture process and can enable the
existing developer frame-advance state. These are test setup writes only, absent from all shipped code. Actual
scene/sky/weather values are read into `/tmp/nd/sky/fixtures.jsonl`. The renderer reads environment state only.

Stage 6 averages **0.216–0.218 ms GPU**, including atmospheric render and full-resolution MSAA composition, across
normal Field/Clock Town/Coast views and times. Fallback scenes incur no stage 6 work. No GL, shader compilation,
link or framebuffer errors in the valid captures. Stage logs live alongside their images.

Remaining manual coverage: a real Clock Town doorway transition, moving/skyward camera sweeps and sun/Moon framing,
active rain in an eligible scene, unusual cutscene sky transitions. Static images do not prove absence of flicker
in motion. Native sun/Moon/stars are preserved by display-list ordering; stars were also seen in the night captures.

## Milestone B: volumetric clouds

The physical sky now includes opt-in cumulus: a 40-step march through a world-anchored 1.6–3.4 km layer,
three coarse sun-visibility probes per occupied sample, four density octaves and a low-frequency weather/coverage
map. An original deterministic 64³ R8 noise volume with mips costs about 293 KiB. Clouds render at half width/height
into premultiplied RGBA16F and use a filtered spatial upscale. Noise mip levels follow the march footprint; a fixed
world-direction sample offset breaks march bands without frame-varying noise. There is no temporal history to leak
across scene cuts, ghost the Moon or require gameplay motion vectors. The previous 3D texture binding is restored.

Wind is fixed at 18/9 m/s using monotonic renderer time, independent of the clock and simulation speed. Its wrapped
1024 km domain matches every density octave and weather-map period. No game state or RNG is written/read for drift.
Clouds share the physical sky's classification/fallback, HDR requirement and strength. No cloud shadows on geometry
are added; native sun, Moon and star draws retain their original ordering and appearance (clouds do not attenuate
those subsequent native draws).

- `gNewDawn.Sky.Clouds`: default **0**, preset **1**; requires Physical Sky and HDR.
- `gNewDawn.Sky.Coverage`: default/preset **0.55**, range 0–1; zero skips the cloud pass.
- `gNewDawn.Sky.CloudStrength`: default/preset **1**, range 0–1; zero skips the cloud pass.
- Menu controls are beside Physical Sky. Stats stage **7** reports clouds separately from sky stage 6.
- Both Physical and Clouds are in the New Dawn preset; integrate milestones A and B together.

Atmospheric quadrature now concentrates samples near the observer/scattering point so low sunlight passes through
the dense lower atmosphere correctly. The horizon-to-fog fade narrows to 0–0.05 in ray Y (about 3 degrees), retaining
more low-angle scattering and sunset glow while still matching fog exactly at the horizon. Native sun emission is
not added a second time.

### Final B evidence

Same hardware/settings as A. All four new/updated programs pass GLSL 330 validation; build, shader asset equality
and whitespace checks pass. Lead reviewed the B diff and approved its structure; the requested 3D binding restore
is included. Local image locations:

- `/tmp/nd/sky/clouds-{morning,noon,dusk,night}/`: all 20 final scene/time images visually inspected. Lit cumulus
  lobes and shaded bodies with blue gaps by day; peach light against cool shaded cloud bodies at dusk; dark clouds
  and native stars at night. Ikana and winter Mountain Village still take their native fallback.
- `/tmp/nd/sky/original-{morning,noon,dusk,night}/`: 20 time-matched frozen-original-build captures, no mods/cvars,
  also visually inspected. These compare the full New Dawn preset/mods against the original, rather than isolating
  the sky. A-only captures above isolate the previous milestone.
- `~/dev/ghm-mods/showcase/sky/{original,newdawn,split}/`: flattened, time-labelled images and original/New Dawn
  splits made with `/tmp/nd/showcase/split.py`. Subdirectories `split/{morning,noon,dusk,night}/` retain point names.
  Requested examples: `split/termina_field_dusk.png`, `split/south_clock_town_morning.png` (and noon/dusk counterparts).
- `/tmp/nd/sky/clouds-drift/`: four one-second-interval captures with game frame **120 frozen**; clouds drift while
  Link, clock and scene remain fixed. A sky patch shifts about **(10,-2) pixels** first-to-last. Its matching MSE
  drops from 19.79 unshifted to 1.16 after that translation. `/tmp/nd/sky/drift-sky-strip.png` shows the sequence.
- `/tmp/nd/sky/clouds-turn/`: eight captures at 0.35-second intervals while turning the normal C-Up camera.
  Every frame inspected; no seams, sudden pattern jumps or evident shimmer in the burst. This coarse burst is
  not a substitute for a continuous high-frame-rate motion check.
- `/tmp/nd/sky/clouds-interior/`: Stock Pot Inn and Woodfall Temple with both toggles on; neither sky GPU pass runs,
  interiors remain intact. Native gameplay/render classification is unchanged.
- `/tmp/nd/sky/clouds-rain-clock/`: day 2 noon Clock Town, **40 rain drops** confirmed from actual environment
  state. Active physical sky/clouds produce the expected grey overcast; precipitation and geometry stay visible.
- `/tmp/nd/sky/{main-coast-sunset,cloud-final-coast-sunset}/`: the same sun-facing Coast view with exact-main and
  new sky. Both show severe orange native glare, so that glare predates this change. Do not use it as a sky showcase.

Across the final 20-point sweep plus drift/turn/interior captures, 300-run averages are:

| Stage | Mean GPU ms | Range of 300-run means |
| --- | ---: | ---: |
| 0 AO | 0.500 | 0.344–0.694 |
| 1 HDR bloom/display/grade | 0.189 | 0.184–0.193 |
| 2 Shadows | 0.284 | 0.247–0.327 |
| 3 Sun shafts | 0.235 | 0.158–0.371 |
| 4 Water/SSR apply | 0.095 | 0.062–0.188 |
| 5 Opaque water copy | 0.131 | 0.113–0.138 |
| 6 Physical sky | 0.220 | 0.219–0.220 |
| 7 Clouds | 0.343 | 0.310–0.415 |

Sky+clouds are about **0.56 ms**, below the 1.5 ms budget in measured views. The separate first-person camera-change
captures reached cloud averages of 0.66 ms (sky 0.22 ms). **Zenith/full-screen cloud cost remains unverified.**
Final matrix logs contain **0 GL, shader build/link or framebuffer errors**. Fallbacks execute neither sky stage.

Remaining limitations: the skyward C-Up fixture failed framing three times and was stopped; explicit giant-Moon,
zenith and sun-facing scattering framing are not verified. Native celestial ordering is preserved by source, and
stars are visible in the night images. The real doorway transition/manual high-frame-rate camera checks from A
remain outstanding. Off-path exact outdoor byte identity remains limited by render-frame noise as documented in A.
