# Volumetric height fog (E2)

`gNewDawn.Fog.Volumetric`: default **0**, preset **0** pending the exact-main off gate recorded in
[SUN_GLARE.md](SUN_GLARE.md). Requires active HDR and Sun Shadows. No gameplay/environment state is written.

The opaque-end HDR pass integrates Beer–Lambert extinction and single scattering over 24 jittered steps at
half resolution. Density falls with fixed world altitude (300–500 units); the game's fog colour and exact
fog-start distance supply the artistic inputs. A Henyey–Greenstein sun lobe is shadowed through the existing
alpha-tested shadow map. The existing irradiance cube contributes a subtle ambient tint when available.
The 16-tap bilateral reconstruction rejects sky and depth discontinuities. RGBA16F stores premultiplied
scattering/opacity; fixed spatial noise and the HDR display dither avoid march/quantisation bands without
frame-varying noise or temporal history. No new dependencies or image assets.

Native distance fog remains active. The new contribution smoothly reaches zero at native full extinction,
so distant silhouettes retain their original fade. Sky pixels are excluded at both resolutions. Fog replaces
Sun Shafts only while the new pass runs. Translucent surfaces drawn afterwards retain their native fog;
this is not a separate volumetric integration for every transparent layer. The existing shadow-map limitations
(culled casters and limited radius) still apply; visibility fades smoothly at the map's XY borders.

The new `uFogVolume` sampler is assigned dedicated unit **14** at program creation. A complete transparent
dummy stays bound when idle, including HDR/fog off. The cube retains unit 12; depth/compare reuse 9/11.
Optional fog shader failures leave the established renderer available.

## Validation, 2026-11-13

Linux Mesa OpenGL, RX 7800 XT, 1920×1080, MSAA 4, preset + full local mods. Canonical
`python3 tools/newdawn/capture.py --bin-dir build/mm --preset --mods /home/loganp/dev/ghm-mods/sets/full ...`
launches each game. `~/dev/ghm-mods/work/glare-fog/session/capture_fixture.py` is a private companion setting time/day and existing
developer frame advance only in disposable test processes. Build: `cmake --build build -j6`.
Both fog programs pass GLSL 330 `glslangValidator`; both default shader asset copies remain byte-identical.

All eight matched before/after views were inspected, including 1:1 crops:
- Termina 08:00 / 17:30: low altitude haze, foreground detail retained, continuous distant fade.
- Cleared Southern Swamp 08:00, Woodfall exterior 08:00: gentle depth separation, native scene colour retained.
- Coast 17:30 and Clock Town 12:00: terrain remains legible; Clock Town density is deliberately lower.
- Cursed Ikana 17:30 and Woodfall Temple 08:00: native fallback, no fog GPU stage.

Final valid sweep: **0 GL, shader compile/link or framebuffer errors** with `MESA_DEBUG=context`.
The first sweep was discarded after a sampler name collided with an existing vec3 sky uniform; the sampler
now has its own name. The first Termina turning burst and its 1:1 crops show no obvious bands/halos; an extended
eight-frame turning burst also shows no obvious flicker or silhouette pops at sampled angles. Static images and coarse bursts do not prove high-frame-rate motion safety.
The frozen Inn on/off pair is byte-identical within this build; both it and HDR-off have zero GL errors.
Comparison to E1 still differs in sparse render noise; the exact-main gate remains open.
Dramatic isolated dusk shafts are **not demonstrated** in the fixed matrix; the shadowed integration is present,
but its visible effect here is principally haze. Interiors and special skies intentionally receive no new fog.

300-run GPU stage means (ms, average of scene means where that stage runs):

| Stage | Before | After |
| --- | ---: | ---: |
| 0 AO | .495 | .495 |
| 1 HDR bloom/display/grade | .190 | .189 |
| 2 Shadows | .308 | .309 |
| 3 Sun shafts | .233 | .180 (fallback scenes only) |
| 4 Water | .102 | .102 |
| 5 Opaque water copy | .137 | .136 |
| 6 Sky | .223 | .223 |
| 7 Clouds | .352 | .352 |
| 8 Sky cube | .074 | .074 |
| 9 Fog march + reconstruct | — | .336 |

Fog stage per scene: Field morning **.343**, dusk **.334**, cleared Swamp **.351**, Woodfall **.349**,
Coast **.336**, Clock Town **.305** ms. Replacing shafts makes the matched extra cost **.064–.135 ms**,
within the +.8 ms budget. Raw per-scene data: `~/dev/ghm-mods/work/glare-fog/session/gpu-stages.json`.

Evidence: `~/dev/ghm-mods/work/glare-fog/session/e2-{before,after-fixed}/`, `e2-crops.png`, `e2-turn/`, `e2-turn-full/`,
and `e2-coast-sun/`. Splits from `/tmp/nd/showcase/split.py`:
`~/dev/ghm-mods/showcase/glare-fog/e2/{before,after,split,coast-sun}/` (left fog off/right on;
both sides already use the New Dawn preset/mods and the initial glare-wash fix; this matrix predates
the modern flare replacement).
