# Art-directed colour

This pass treats New Dawn as a remaster: each region has a deliberate palette, with recognisable characters, places and gameplay colours. It builds on the existing HDR tonemap, fog, water and global grade. No geometry, collision, animation, actor logic or normal gameplay clock writes are involved.

## Runtime look

`gNewDawn.Grade.ArtDirection` enables the grade; default **0** outside the preset. `gNewDawn.Grade.ArtStrength` scales it from identity (0) to 1.5; normal strength is 1. The verified grade is **enabled by the New Dawn preset**, with strength 1. The controls are under the existing post-processing settings. Disabling post-processing also bypasses art direction.

The game-side table is in `mm/2s2h/Enhancements/Graphics/NewDawnPostFx.cpp`. Explicit scene groups override the outdoor/interior fallbacks; the Woodfall clear flag selects the exterior's poisoned or restored palette. Fixed-light interiors retain their region mood without the outdoor clock treatment.

| Area | Direction |
| --- | --- |
| Poisoned Southern Swamp / Woodfall exterior | Restrained colour, violet shade, yellow-green vegetation; poison stays purple |
| Cleared swamp / Woodfall / Deku Palace | Golden-green foliage, warmer light, restored colour |
| Woodfall Temple / Woods of Mystery | Humid green-gold stone, visible moss and carved detail |
| Ikana Canyon / Graveyard / castle | Bleached stone, subdued vegetation, cool haunted shadows |
| Stone Tower / upright temple | Sandstone and rust against violet shade |
| Inverted tower / temple | Cold violet-blue balance |
| Great Bay / temple | Clear coastal colour; cool teal metal and warm lamps in the temple |
| Snowhead / winter mountains | Blue-white snow with readable surface detail |
| Clock Town | Warm daylight; cool night shade against warm highlights |
| Ordinary interiors | Warmer timber, added depth and controlled saturation |

The four outdoor bands use smoothstep interpolation: night through 04:30, dawn at 06:00, day at 09:00–15:30, dusk at 17:30, night again at 19:30. The curve wraps continuously at midnight. The target look eases by 6% once per game tick, including scene changes (about 2.4 seconds to cover 95% of a step at 20 Hz). Render interpolation does not advance that state again. Strength zero and feature off bypass the shader block.

The renderer applies the grade after tonemapping/sRGB and before display dither and HUD: selective green-to-gold/teal adjustment, slope/offset/power, bounded S-curve contrast, luminance-keyed split tone and saturation. It reuses the existing display pass, adding no render target or fullscreen pass. The contrast curve is bounded to avoid reversing highlights above display white. Warm/cool separation is luminance-based; it does not identify individual lamps or introduce physical light sources.

## Validation and evidence

Verified build commands in this fork/worktree:

```sh
cmake --build build -j6
cmake --build build --target Generate2ShipOtr
```

All image runs use `tools/newdawn/capture.py --bin-dir build/mm --preset --mods /home/loganp/dev/ghm-mods/sets/full`, `MESA_DEBUG=context`, gamescope headless and 1920×1080 on the RX 7800 XT. Scratch and full-resolution sources live in `/home/loganp/dev/ghm-mods/work/color/`. Deliverable labelled splits and full pairs live in `/home/loganp/dev/ghm-mods/showcase/color/`.

Capture time overrides must use a **float**, for example `--cvar gNewDawn.Debug.BootHour=22.0`; integer cvars are a different type and are ignored by the float reader. The original integer-hour night/dawn attempts were discarded. `gNewDawn.Debug.BootWoodfallCleared=1` supplies the restored Woodfall fixture only in disposable boot-to-point sessions. Extra viewpoints were derived from the local base archive's spawn/entrance lists; Snowhead's viewpoint is moved inside the doorway to avoid its exit trigger.

The extracted production look/weight functions passed an isolated C++/UBSan check: 24,001 clock samples sum to one, all keys and midnight are continuous, supported strengths stay finite with positive slope/power/saturation, strength zero is identity, and scene mixing converges.

### Off identity

Against a freshly built, unmodified main `f7f8b1f1c` / renderer `e4aba70e`, Stock Pot Inn at 12:00 frozen at game frame 30 with `gMatchRefreshRate=0`, `gInterpolationFPS=20`, the same preset/full archives and art direction off: two repeats of each binary in HDR and LDR are **byte-identical** (all eight screenshots). HDR SHA-256: `46ad33f92c94c188f017961d297107a7295ca957e83e1ce39ec54ea04103329d`; LDR: `f8599206cd7f8a60f248349b1f062f905e823aadba73ee30a6344eea1893b391`. The fixture writes only to disposable capture processes. `identity.json` records each path and hash. Live captures have animation/weather/timing noise and are not claimed pixel-identical.

### GPU

Matched 15-second off/on captures, same RX 7800 XT, 1920×1080, MSAA 4, full mods, morning scene/settings. Each logged GPU timer sample averages 300 runs; the table averages available samples (two or three per setting). This timer includes bloom, display transform and grade, rather than total frame time.

| Scene | Art off (ms) | Art on (ms) | Added (ms) |
| --- | ---: | ---: | ---: |
| Termina Field | 0.1883 | 0.2095 | 0.0212 |
| Stock Pot Inn | 0.1870 | 0.2080 | 0.0210 |
| Winter Mountain Village | 0.1870 | 0.2100 | 0.0230 |

Raw samples: `work/color/timings.json` and `perf-{off,on}/logs/`. There is no extra pass, target or texture lookup for the runtime grade.

### Motion and limits

The private fixture accelerated Clock Town from late afternoon through dusk and captured six one-second steps. A separate eight-frame sequence transitions from the Inn to Ikana. Both were visually reviewed for grade discontinuities alongside the production-function continuity/convergence proof. These scripted checks cover colour interpolation, not interactive pause/pictograph/resize behaviour or every cutscene and weather combination. Existing animations and gameplay code are preserved; no full gameplay regression playthrough is claimed.


### Capture coverage

The final runtime comparison set contains 32 labelled pairs/splits, all visually reviewed with fog enabled:

| Showcase directory | Scene/time coverage | Full-resolution after sources in `work/color/` |
| --- | --- | --- |
| `runtime-day/` | Original 14 viewpoints at 08:00 | `verified8/` |
| `runtime-day-extra/` | Graveyard, Stone Tower, inverted temple, Snowhead Temple at 08:00 | `verified8/` |
| `runtime-night/` | Town, Ikana, Graveyard, winter Village, both swamps, Stone Tower at 22:00 | `verified-night/` |
| `runtime-dusk/` | Town, Ikana, Stone Tower, cleared swamp at 17:30 | `verified-dusk/` |
| `runtime-dawn/` | Town and winter Village at 06:00 | `verified-dawn/` |
| `runtime-woodfall-cleared/` | Restored Woodfall exterior at 08:00 | `verified-woodfall/` |

The 48 final/runtime-proof logs (32 area looks, six timing runs, eight off-identity runs and two interpolation sequences) report **0 GL errors, 0 shader errors and 0 null textures**. See `gl-errors.json` for the audited folders. These runs use the original 4K albedos, keeping the runtime change independently reviewable.

Post-build control checks also passed: strength zero reproduces main's HDR fixture byte-for-byte; the master post-processing switch bypasses art direction exactly; art direction works alone in LDR with both bloom and the ordinary grade disabled; and a capture using the preset without an explicit art-direction override shows the enabled look. `controls.json` and the six control/preset capture directories record these checks.
