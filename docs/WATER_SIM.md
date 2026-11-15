# Water simulation (roadmap item 3)

Visual only. The game's WaterBox height stays the authority for swimming, buoyancy and collision: the renderer reads the
planes, the player position, other actors and the game's own splash effects, and never writes anything back.
Base: main `fc8459f1a`, libultraship `82d59e05`; merged with main `de79c10e2` and new-dawn `322f6903` (perf overlay). OpenGL only, needs HDR and **Water Restyle** (the displaced surface is re-lit by
the restyle pass). Evidence: `~/dev/ghm-mods/showcase/water-sim/`.

## Controls (Advanced → Water; Ultra and High looks have them on)
- `gNewDawn.Water.Waves` + `WaveStrength` (0–2, default 1): ocean swell on **Great Bay Coast and Zora Cape only** (`sNewDawnOceanScenes`).
  Lakes, ponds, rivers and interiors keep the flat plane and their small wind ripples.
- `gNewDawn.Water.Ripples` + `RippleStrength` (0–2, default 1): interaction ripples and wake foam, in every water.
- Debug views (`gNewDawn.Water.Debug`): 3 swell height (orange above the plane, blue below, grey = none), 4 slope/foam, 5 swash
  distance to the waterline. Use with Reflections and Glints off.

## Design
Decision: **ray-march a height field in the existing screen-space water pass**, not a tessellated ocean mesh. The water stage is
already a per-pixel plane intersection that re-shades the native flat mesh, so a displaced surface is a better ray/surface
intersection plus a normal; no second geometry path, no depth-buffer plumbing, depth/occlusion/fog stay consistent with the
existing foam, refraction and SSR. Silhouette-level displacement appears as crests hiding the water behind them (parallax),
a displaced waterline against the sky, and crest/trough shading and reflection.

Passes, all inside the water stage (`WATER_XLU`, one GPU timer), in order:
1. **Ripple step** (`kWaterRippleShader`): a 256² RGBA16F height/velocity/foam field, 4 units per texel (1024² units) that follows
   the player in whole texels (no swimming), semi-implicit wave equation (c = 110 u/s, damping 0.9/s, absorbing border) stepped at
   a fixed 1/60 s (max 4 steps/frame) from the interpolated `waterTime`; it restarts on a scene change or a clock gap.
   Wake foam (B channel) is raised where the surface is agitated hard (ring crests) plus a narrow line under a moving source, and
   fades in about a second, so a swimmer leaves a thin broken trail. Sources are rate-based (strength × dt) so the several rendered frames of one game tick do not multiply them: Link in the
   water (speed-scaled, alternating sign so a held source emits rings), swimming/wading actors within 600 units, and the
   game's own `EFFECT_SS_G_SPLASH` / young `EFFECT_SS_G_RIPPLE` entries (read-only table scan). Up to 16 sources.
2. **Depth map** (1/8 res R16F): blurred water depth under each pixel (plane minus opaque bed; land counts as 0).
3. **Wave trace** (full res RGBA16F, texture lane 31): per water pixel a 14-step + 4-step regula-falsi march between plane ±
   amplitude bound of `h = Σ A_i f_i env_i (exp(1.4 (sin θ_i − 1)) − mean)`: six sharpened-sine (trochoid-like) swell trains of
   470/270/160/105/68/44 units, deep-water dispersion (c ∝ √λ), directions = the shared wind (`newdawn_wind.h`, fixed 2:1
   direction; gust scales amplitude 0.8–1.2 and whitecaps) rotated by 0 / +32° / −49° / +66° / −80° / +16°, a slow noise phase
   warp, and a **per-train envelope** (product of two long sines, 0.2–1.6) so trains come and go and crests break into irregular
   patches. The march uses the three long trains (they carry the displacement); the three fine ones only shape the normal. This is a
   Gerstner-style height field, not true Gerstner: the horizontal displacement is dropped because a ray march needs h(x, z).
   Output: surface height above the plane, slope, and 1 + foam (A = 0 means "no wave", the pass falls back to the flat plane).
   Each train fades out when a pixel covers more than ~3–30% of its wavelength **along the view direction** (footprint /
   sin(elevation)), so distant water converges to a smooth, glossy surface with no banding and nothing aliases.
   Amplitude (13 units at strength 1) is also tapered by the depth map (`smoothstep(10, 70)`) and by camera height (calm when
   the camera is within a few amplitudes of the surface), so waves vanish over shallows, beaches, piers and rocks. The ripple
   field adds up to ±4 units (tapered to 0 on the beach) and its slope.
4. **Surface pass** (existing, modified): uses the displaced hit for thickness, path length, refraction and foam; facet lighting
   from the swell normal, a turquoise glow through thin crests, whitecaps (patchy, noise-gated, from the crest height and wind,
   faded with the stretched footprint) and the wake foam. Foam is only opaque where two fine noises agree (max 0.65), so it reads
   as thin, broken streaks and lace, never as a sheet. **Swash**: ground within 14 units above a plane is wetted and carries a lacy front that climbs and drains
   (distance to the waterline = height over local slope from the world normal; skipped for walls and perfectly level decks).
5. **Glint/SSR pass** (existing, modified): hit point, glint normal, reflection normal and SSR origin come from the displaced surface.

Samplers: new dedicated lanes 31 (wave buffer), 32 (depth map), 33 (ripple field), each with a complete dummy bound at program
creation and restored after the pass. The Init texture-unit check is now `> kRippleUnit`. Grass owns 29/30; if another feature
takes 31–33 this needs re-numbering.

## Ocean streaks and saturation (user report)
Diagnosis (Coast, waves off, one suspect disabled at a time, see `ocean-streaks-1to1-before-after.png`):
the horizontal lines were the **native animated water texture's high-pass detail** kept by Restyle (it stretches into streaks at
grazing angles) and the procedural slope octaves whose footprint fade ignored the grazing stretch (they collapsed into
streaks / a diamond lattice). The vertical smear in the SSR gather and the glints were not the cause (disabling them leaves the
lines). Second round (after review: stripes were still visible in the 1:1 view): the remaining bands were the long swell trains seen at
a grazing angle (three trains with similar directions made parallel crest lines) and whitecaps aligned to those crests. Fix: six
trains with wide, uneven directions and independent patchy envelopes; trains fade by the grazing-stretched footprint; whitecaps
are gated into patches and fade with distance. See `ocean-streaks-1to1-before-after.png` and `ocean-full-width-after.png`.
First round fix: the native detail is faded by view elevation (`smoothstep(0.15, 0.5, V.y)`), every slope octave fades by the
stretched footprint `footprint / sin(elevation)`, the six facet waves get a second phase warp, and the swell supplies the
large-scale normals instead.

Palettes retuned (`sNewDawnWaterPalettes`): the deep body colour carries little chroma and green/blue absorb more slowly, so
open water is dark and grey-blue-green and only shallows over a bright bed turn turquoise. Zora Cape now uses the Great Bay
palette. HSV saturation of the sRGB-encoded deep body colour, before → after (analytic, `palette-saturation.txt`):
Great Bay 0.73→0.44, Zora Cape 0.63→0.44, generic (Ikana river, Mountain Village spring) 0.63→0.43, Termina/Clock Town
fountain/Romani Ranch 0.48→0.31, Southern Swamp cleared 0.40→0.29, poisoned 0.57→0.36, Zora Hall 0.58→0.48, Great Bay Temple
0.66→0.53, Snowhead 0.65→0.49, Stone Tower 0.58→0.42. Measured on screen (noon, 1:1 crops, mean HSV S):
Great Bay open ocean 0.49→0.36 and 0.55→0.36, from-the-sea view shallows 0.83→0.46, mid water 0.53→0.38; Termina pond 0.45→0.40.
The user's reference crop measures S 0.86–0.91 (rgb ≈ 14,92,122) in their build.
Not captured on screen: Clock Town fountain, Laundry Pool, Romani Ranch, Ikana river, Mountain Village spring (the fixtures
did not show their water; their numbers are the analytic ones).

## Validation (Linux, Mesa radeonsi RX 7800 XT, GL 4.6, 1920×1080, MSAA 4, preset + full mods, `MESA_DEBUG=context`)
- Off identity: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`, the AGENT-RULES off set
  plus Fog.Volumetric/SunShafts/Sky.Physical/Sky.Flare/Lighting.Local 0): main ×2 and this build with Waves=0, Ripples=0 ×2 all
  byte-identical (SHA-256 `705ef49b…`). With only the shorter cvar set, main vs main already differs by ~240 px.
- GL: 0 errors in every capture (`PostFx.Debug.Stats=1` error check, Mesa debug context); GLSL compiles on radeonsi.
- GPU water stage (`PostFx.Debug.Stats=1`, steady state, merged build; the GPU was shared with other agents, so ±0.1 ms):
  Coast, low camera over the sea (most ocean on screen): off 0.63–0.72 ms, waves 1.10–1.24, ripples 0.91–1.01, both 1.10–1.22.
  Zora Hall: off 0.45, waves 0.45 (no ocean), ripples/both 0.91–0.93. So about +0.5 ms for waves and +0.45 ms for ripples (they
  share the trace pass; both together cost about the same as either), roughly 3% of a 60 fps frame. Both are on in Ultra and High.
- Smoke test after merging main/new-dawn: 0 GL errors (Stats error check, Mesa debug context) at the Coast pier view;
  Link wading and Zora Link (form poked in a private fixture, `--form 2`) wading/swimming into the sea show thin ring ripples and
  no foam sheet (`burst-swimming-before-after.png`, `burst-zora-swimming-before-after.png`, `wake-strip-before-after.png`). Zora Link
  fast-swimming with the swim button was not driven (the fixture only holds the stick); the source strength follows actor speed.
- Images: before = origin/main build, after = this build, same preset, mods, fixtures: `burst-beach-before-after.png`,
  `burst-pier-before-after.png` (a low camera at pier height over open sea, placed by a fixture poke; no walkable pier geometry
  was located), `burst-zora-cape-before-after.png`, `burst-swimming-before-after.png`, `burst-zora-swimming-before-after.png`, `wake-strip-before-after.png`,
  `shoreline-cliff-1to1-before-after.png`, `shoreline-sand-1to1-before-after.png`, `ocean-streaks-1to1-before-after.png`.

## Limits and deferred
- Rivers and waterfalls: see the section below (rivers done; no real waterfall located).
- Swash is implemented but **not visually verified**: the Great Bay Coast / Zora Cape shores that were reachable are shelves and
  cliffs (the beach sand is far above the plane), so no ground fell inside the 14-unit window (debug view 5). Cliff bases and
  decks are excluded by the normal tests. Expect to tune it on the first real gently sloping beach.
- Silhouette displacement is limited to the water: crests hide water behind them and the waterline against the sky moves, but
  the swell never covers a pier, beach or rock (zero amplitude where the blurred depth is shallow), and it does not displace the
  scene's own geometry. Near a pier pile the taper pulls the swell down within a few pixels.
- Screen-space limits: the taper uses the depth of the bed seen through the pixel, a rock under deep water briefly calms the
  waves behind it; far wave bands foreshorten into horizontal bands at the horizon (real waves do, but the pattern is regular).
- Underwater (#61) and caustics keep the flat plane: seen from below the surface is not displaced, and caustics are computed
  against the plane height. The camera inside the swell (low swimming camera) fades the swell out instead of cutting.
- Ripples: window is 1024² units around the player (actors outside it have no wake); the Link source follows the 20 Hz game
  position, not an interpolated one; waves are tied to game time, so a paused game freezes them.
- Whitecaps and wake foam are in-shader noise, no foam texture asset was added. Far water beyond ~1500 units is intentionally
  smooth (the swell fades out); the displaced horizon is therefore subtle.
- Zora Cape was captured from a camera placed past the cave mouth; the shore scene there was not reachable on foot in the fixtures.
- Possible later optimisation: scissor the trace pass to the ripple window when waves are off (ripples-only cost at Zora Hall).

## Rivers and waterfalls (`feature/rivers-waterfalls`)
Controls (Advanced → Water; Ultra and High): `gNewDawn.Water.Rivers` + `RiverStrength` (0-2), `gNewDawn.Water.Waterfalls` + `FallStrength`.
Evidence: `~/dev/ghm-mods/showcase/rivers/`.

**Flow direction comes from the game's own texture scroll.** The interpreter (`RecordFlowTriangle`) reads the per-(texture, tile)
`uls/ult` offset the game advances every 20 Hz tick (`gDPSetTileSize`); a step that repeats twice in a row counts as a scroll
(actor and atlas textures never repeat, so they are ignored). Each triangle's own UV gradient turns the texel velocity into a
world velocity (`dP/du*su + dP/dv*sv`, mean of the two render tiles), so bends, speed and direction are the artists' own and no
per-scene table is needed. River triangles are the ones lying in a WaterBox plane (±3 units, level); painted streams with no
WaterBox need an entry in `newdawn_river_textures.inc`; waterfall faces are listed by texture in `newdawn_fall_textures.inc`
(`gNewDawn.Water.FlowDebug=1` logs every scrolling texture and its extents (`FLOWBOX`), `=2` also paints recorded falls magenta and
streams cyan; `gNewDawn.Water.FallTextures=<substr,...>` forces textures to be treated as falls). Ocean scenes (Coast, Zora Cape)
never get river flow (their scrolling texture is not a current). Read only, nothing is written back.

**Rivers.** The recorded river triangles are re-rasterised from the camera into an RG16F flow buffer (units 36, world xz velocity,
B = covered) before the water surface pass. The surface pass advects a committed tileable noise
(`mm/assets/newdawn/water/flow_noise_256.png`, made by `tools/newdawn/water-flow/gen_flow_noise.py`, CC0) with a two-phase flow map
(2.6 s cycle), blurred along the flow with 5 taps so it stretches into streaks without rotating the sampling frame (a rotation
about the world origin would jump at every triangle with a different direction). The result is added to the refraction slope and
the facet lighting (visible sheen and bed distortion), and its ridged mask makes streak foam, strongest where the water is shallow
(banks, rocks) and fast, on top of the existing depth-based contact foam. Colours stay the #98 palettes. Debug view `Water.Debug=6`
shows the mask and flow direction. Verified on the **Ikana river** and the **Mountain Village spring pool**
(`strip-*-off-over-on.png`: top row off, bottom on, 4 frames 0.6 s apart; `crop1to1-*`). The Southern Swamp (poisoned and cleared) has
a scrolling water texture (flows (50,-25) and (12,-12) units/s) and is recorded, but my fixture positions only showed land, so it
is **not shown on screen**.

**Waterfalls (mechanism only).** The recorded fall faces get a second layer on top of the native texture (streaks of the same noise
streamed down the face, sun glints, a back-lit translucency term, foam bands, hand depth-tested against the scene depth), and each
fall gets a foot emitter (lowest vertex per 48-unit cell of a face taller than 36 units): 20 rising mist billboards, 8 ballistic
spray droplets lit by forward scattering of the sun, 2 ragged foam discs, soft against the depth buffer, using the committed CC0
Kenney `atmos/dust_64.png`. The overlay, the mist and spray were seen working (debug paint and puffs on stand-in geometry), but
**no real waterfall has been located and verified**: the scrolling vertical textures found by scanning were the Astral
Observatory's force-field barrier (Termina Field `02E058`, `037A98`, `0240D0`), ice or crystal pillars (`02C850`), sea-spray wedges
(Zora Cape `015DF0`), and a sky effect (swamp `00F360`). Mountain Village spring `Z2_10YUKIYAMANOMURA2_room_00Tex_01C488` (a 900-unit
tall face) was not seen. Only `gGreatBayTempleObjectWaterfallWaterTex` (the temple's water-wheel falls, a real fall by name) is
listed and it has not been viewed. Termina Field, Ikana, Great Bay and Woodfall falls are therefore **not delivered**; the next step
is to stand the camera at a known fall, add its texture to `newdawn_fall_textures.inc` and confirm with `FlowDebug=2`. The Romani
Ranch and Milk Road streams, and the Woods of Mystery, had no scrolling river texture in the scan.

**Cost.** Water stage, `PostFx.Debug.Stats=1` (RX 7800 XT, 1080p, shared GPU): Ikana 1.613 → 1.687 ms, spring pool 1.484 → 1.656 ms,
swamp (land view) 0.555 → 0.581 ms. 0 GL errors in the Stats error check. **Off identity not demonstrated**: after merging main
(torch light) the frozen Stock Pot Inn fixture differs by about 256,000 pixels between two identical runs, so on/off (no water in
the Inn) cannot be told from noise. With the toggles off no flow triangles are recorded and no pass runs; the only unconditional
change is one extra sampler lane pair (36, 37) and an uncalled branch in the surface shader.

**Limits.** Streaks are stretched noise (lumpy rather than long strokes from above); no speed-up in narrow sections and no eddies
behind rocks (the scroll gives one velocity per triangle); the glint/SSR pass still uses its own scrolling normal; painted streams
without a WaterBox get an overlay only (no restyle). A scroll detected from a texture that two meshes scroll differently in the same
tile slot would be wrong (not seen).
