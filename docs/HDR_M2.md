# HDR milestone 2 validation

2026-11-12, branch `feature/hdr-m2`, based on superproject `67cec33f6` and libultraship `46f78757`; HDR-off comparison main is `2d8abb17f` (material tooling added after M1).
OpenGL 4.6, AMD Radeon RX 7800 XT (radeonsi/ACO), gamescope headless, 1920×1080, 60 Hz capture environment.
Only rendering code, preset/menu controls and documentation change. Gameplay, animation, collision and clock code are unchanged.

## Pipeline

RGBA16F world → sun attenuation → linear scene AO → additive shafts → linear water/SSR → RGBA16F resolve → up-to-six-level float bloom downsample/upsample → additive bloom → exposure/ACES-like fit → sRGB encoding → display grade → optional dither → legacy RGB8 presentation/HUD.
The bloom chain uses as many levels as the resolution permits (up to six). Its HDR threshold is at least 1.0 with a soft knee; LDR retains its original threshold/normalisation and screen blend.

MSAA retains the original game's depth/stencil renderbuffer. Only colour swaps to a same-size RGBA16F multisample renderbuffer. Format-specific sample support is queried on GL 4.2+, and framebuffer completeness is checked before enabling HDR. Unsupported HDR configurations restore LDR as a whole.
Early framebuffer copies, CPU captures and framebuffer-texture consumers receive a single-sample display snapshot, without ending the HDR world phase or replacing depth.

Preset: exposure −0.2 EV, sun 1.8, ambient fill 0.55, sky/fog intensity 0.85, AO strength 1.2, shadow strength 0.85/floor 0.30, shafts 0.18, bloom strength 0.18/threshold 1.0, MSAA 4.
HDR defaults remain opt-in outside the preset. The menu exposes HDR, exposure and dither; existing effect toggles remain independent.

## Commands and local evidence

Verified build commands for this worktree:

```sh
cmake --build build -j16
cmake --build build --target Generate2ShipOtr
python tools/newdawn/capture.py --help
```

The capture suite uses `--bin-dir build/mm`, `--mods /home/loganp/dev/ghm-mods/sets/full`, `--cvar gEnhancements.Mods.AlternateAssets=1` and `--cvar gNewDawn.PostFx.Debug.Stats=1`, plus every value from `sNewDawnPresetCvars`. Full setting manifests and orchestration scripts are under `/tmp/nd/hdr-m2/` (not committed).

Evidence directories:

- `m1/`: all 12 points with the previous preset plus M1 HDR, MSAA 1; captured before changes.
- `verified/`: final all-12-point M2 preset, MSAA 4. `m2/` and `m2-final/` retain earlier iterations for diagnosis.
- `verified-msaa1/`: final matched single-sample performance check; `m2-msaa1/` and `m2-final-msaa1/` retain earlier scene checks.
- `main-off/` and `m2-off/`: same previous preset, HDR off, MSAA 1, all 12 points, same mods; main baseline binary from `/home/loganp/dev/2ship2harkinian/build-cmake/mm`.
- `m1-attract/` and `verified-attract/`: title-sequence captures at 20/48/76 seconds.
- `water-angle/`: reversed coast/hall views.
- `depth-on/` and `depth-off/`: explicit MSAA 4 gameplay depth-query logs, same scene/camera. Single and batch calls return identical sequences (20 logged values over two probe frames).
- `still-main/` vs `still-m2-bounded/`: byte-identical Termina Field, same RNG seed, tick 120 and 08:11 game time. The debugger stops each disposable process before tick 121; no source-level simulation change.
- `still-main-more/` and `still-m2-more/`: additional matched-frame town/canyon/interior comparisons.
- `night/` and `dusk/`: debugger-only clock overrides at boot, followed by 120 normal ticks, then a stopped-frame screenshot; night field/town and dusk field.
- `float-inspect/`: single-sample floating-point scene inspection (excluded from performance measurements).

## Results

- Build and asset generation passed. Both shader sources and the shader inside `build/mm/2ship.o2r` match after newline normalisation. Changed C/C++ lines were formatted; whitespace checks passed with CRLF recognised for the shader files. Independent renderer review found no remaining blocking issue.
- Visually inspected all 12 final preset images against M1, plus reversed water views, title sequence, dusk and night. Shadows ground objects in the coast/snow/town/swamp; water reflections are visible in Zora Hall. Sky/cloud/snow detail remains visible, interiors remain readable, and no new halos were accepted. The Ikana distant silhouette caught during review was removed by the HDR shadow distance fade.
- HDR off is byte-identical to main in four RNG-seeded, stopped-frame scenes: Termina Field, South Clock Town, Ikana Canyon and Stock Pot Inn. All four have zero changed pixels at 1920×1080. The full live 12-point off sweep also ran; wall-clock captures include animation/timing noise, so exact equality is claimed only for the four matched frames.
- Explicit MSAA 4 `GetPixelDepth` tests exercised both single-coordinate and nine-coordinate requests. All 20 logged results over two probe frames match between HDR on/off; no GL error was reported by the probes.
- Float inspection after the opaque effects reports peaks 1.39–1.91 and 839–2133 pixels above 1 in Termina Field. This is scene-linear headroom before tonemapping, not inference from image brightness.
- Final preset, MSAA 1, title, dusk/night, depth and float-inspection logs: **0 reported GL errors and 0 null textures**. The temporary filesystem filled during extra stopped-frame captures; redundant binary copies were removed and all affected images were recaptured and checked for valid PNG data.

Termina Field GPU timer averages, same GPU/resolution/mods; each timer sample averages 300 runs:

| Timed passes (ms) | M1 HDR, MSAA 1 | M2, MSAA 1 | M2, MSAA 4 |
| --- | ---: | ---: | ---: |
| Shadows | gated off | 0.235 | 0.291 |
| AO | gated off | 0.352 | 0.342 |
| Shafts | gated off | 0.229 | 0.227 |
| Opaque water colour/depth copy | gated off | 0.104 | 0.207 |
| Water/SSR | gated off | 0.052 | 0.051 |
| Bloom/display transform/grade | 0.032 (transform only) | 0.164 | 0.210 |
| **Sum of timed effect passes** | **0.032** | **1.136** | **1.328** |

These sums exclude world rendering, initial effect-depth copies, final display-copy/presentation and remaining frame work. They are not whole-frame GPU costs. At MSAA 4 the same timer sum is 1.320 ms in Great Bay Coast and 1.049 ms in Stock Pot Inn. Settings differ intentionally between M1 (effects gated off) and M2 (full preset); MSAA 1 is the matched sample-count comparison.

Final before/after pairs: `/tmp/nd/hdr-m2/comparison-final/`. Setting manifests, logs, per-scene GPU data, exact pixel comparisons and depth comparison JSON are in `/tmp/nd/hdr-m2/`.

## Changed files

Backend: `libultraship/include/fast/backends/gfx_opengl.h`, `gfx_opengl_postfx.h`, `include/fast/newdawn_postfx.h`; `src/fast/backends/gfx_opengl.cpp`, `gfx_opengl_postfx.cpp`; `src/fast/shaders/opengl/default.shader.glsl`.

Game: `mm/2s2h/BenGui/BenMenu.cpp`, `mm/2s2h/Enhancements/Graphics/NewDawnPostFx.cpp` and `.h`, `mm/src/code/z_vr_box_draw.c`, and the packaged `mm/assets/custom/shaders/opengl/default.shader.glsl`. Documentation: this report, `docs/STATE.md`, `docs/DECISIONS.md`; the superproject also advances the libultraship pointer.

## Limits

AO currently attenuates the full linear scene. Shadow direct/ambient separation is approximate; attenuation fades out by the camera-centred shadow radius and with the global fog estimate; it can also attenuate point light/emissive contributions. Screen-space reflections retain off-screen misses, half-resolution reconstruction and the existing WaterBox surface approximation. Unsupported combiners and unlit fire/sun sprites remain bounded artistic inputs; the bloom soft knee can include them but does not create physically calibrated emission.
Deku-bubble refraction retains M1's whole-frame LDR fallback. No new display-source material classification is introduced.
MSAA 4 captures and real depth-query logging do not substitute for interactive aiming, pictograph scoring, repeated pause, window resize or transitions; those cases require separate manual validation. GPU pass timings do not measure total game cost.
