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
