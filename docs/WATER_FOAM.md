# Water foam, refraction and restyle

**Follow-up 2026-11-14:** dusk SSR-speck reproduction stopped after three failed attempts on current main.
No water/filter change was retained and no fix is claimed. See the [renderer investigation](../libultraship/docs/NEW_DAWN_FOLLOWUPS.md)
for the tested views and capture limitations.

Visual-only OpenGL water extension, based on main `bf1c48e68` / libultraship `33c48852`.

## Controls and rendering

- `gNewDawn.Water.Foam`, `.Refraction`, `.Restyle`: default **0**, independent menu toggles. Foam and refraction have strength sliders (default 1, range 0–1). Refraction and restyle require HDR; foam also works in LDR.
- The existing collision WaterBox planes are read, never modified. The new surface pass runs before the existing glint/reflection overlay. Its opaque colour/depth snapshot supplies the bed; scene-end depth rejects foreground objects. The existing SSR `strength * sqrt(F)` and sky `0.4 * strength * F` weights are unchanged.
- Foam uses moving world-space noise and wash bands at small water/bed thickness. A thickness gradient estimates distance to contact, with footprint filtering and distance fade. Its colour follows sunlight, sky and scene fog.
- Refraction offsets the opaque bed by travelling wave slopes, vanishes at contact, and fades away in deep water. All four bilinear colour taps validate opaque and visible depth. Invalid weight falls back to the original bed, preventing foreground colour from being pulled into the water.
- Restyle uses `bed * exp(-path * absorption) + scatter * (1 - exp(-path * absorption))`, sunlight and a directional subsurface term. A small scene table selects ocean blue-teal, clear green ponds (including Laundry Pool), tannin brown-green cleared swamp, purple-magenta poison, crystal blue Zora Hall, and themed temples. The painted colour is suppressed; masked high-frequency ripple and bright particle detail remain. Fog applies to the completed result. Screen transitions, game-over fills and world covers suppress the new effects; native sun glare attenuates their contribution.
- New surface samplers receive distinct postfx units 8–11 at program creation, each initially bound to a complete dummy texture. Actual captures replace those bindings before drawing. No external water textures or assets are required.

## Blurry water investigation

The previous SSR applied two spatial blurs: a five-sample cross at 2.5 full-resolution pixels, then a 16-tap Gaussian upsample from half resolution. Restyle reduces the cross to 0.65 pixels and uses four depth-aware bilinear upsample taps. SSR remains half resolution and deterministic; hit confidence, Fresnel weights, raymarch and the sky-cube weights remain unchanged. Six travelling-wave scales (70 down to 3.4 world units) add procedural surface detail with footprint fade. The two original glint octaves and texture sampling settings are unchanged. The preset has 16× anisotropy and LOD bias 0; no mip/anisotropy defect is inferred from the SSR blur.

## Validation

Build commands verified in this worktree:

```sh
cmake --build build -j6
cmake --build build --target Generate2ShipOtr
```

Captures use `tools/newdawn/capture.py`, `--bin-dir build/mm --preset --mods /home/loganp/dev/ghm-mods/sets/full`, and `MESA_DEBUG=context`. A private wrapper in `~/dev/ghm-mods/work/water/run.py` registers water viewpoints and pins time / frame only in disposable capture processes. It also drives the look camera. Those fixtures and screenshots remain local.

Hardware: RX 7800 XT, Mesa radeonsi, OpenGL 4.6; 1920×1080, MSAA 4, full preset and mods. Independent renderer review covered depth sampling, compositing, fog, sampler initialization, palettes and screen fills. Shader source also passes `glslangValidator`.

### Final evidence

- **Off = main, deterministically.** Stock Pot Inn, time 12:00, frozen at game frame 30 with interpolation fixed at 20 FPS (`gMatchRefreshRate=0`), against an unmodified main binary (`dd4c4a108` build, same archives). Two repeats each of main and ours with `Foam/Refraction/Restyle=0`, in HDR and LDR: all four screenshots per mode have identical SHA-256 (HDR `46ad33f9…`, LDR `f8599206…`), 0 GL errors (`MESA_DEBUG=context`).
- **Blur fix vs main**, Great Bay Coast, 1:1 crops, main above / new below: `showcase/water/crops/vs-main-{noon,dusk}-{near,mid}.png`. Ripple and tower-reflection edges are visibly crisper; the beach/water edge is now distinct.
- **Before/after splits** (left = all three cvars 0, right = on, same frame): `showcase/water/split/final/{noon,dusk}-{coast_water,termina_water,swamp_water,poison_water,zora_hall}.png`, contact sheets `sheet-noon.png`, `sheet-dusk.png`. All captured with the full preset and mods; no GL errors.
- **GPU** (`gNewDawn.PostFx.Debug.Stats=1`, Coast, RX 7800 XT): water stage 0.333 ms off → 0.365–0.476 ms on; with the opaque copy about +0.14 ms total, inside the +0.4 ms budget. The sharper SSR filter alone is about 0.08 ms cheaper than the old one.
- **Motion:** foam moves across a pond burst; a Coast turning burst shows no flashes or foreground smears.

## Limits

- Screen-space refraction cannot reveal off-screen or occluded bed geometry; invalid samples use the undisplaced bed. Underwater cameras retain the existing path.
- The scene-end pass cannot perfectly separate broad translucent effects from the painted water. It retains fine bright/chromatic details and attenuates sun glare; opaque/visible depth protects actors and banks. Full particle/weather/cutscene coverage remains unverified.
- WaterBox masks approximate render meshes; sculpted surface displacement, geometric breakers and off-screen geometry reflections are outside this change. Poison remains purple and has one tenth normal refraction strength.
- Temple palettes are defined but their individual rooms have not received a full visual sweep.
- Laundry Pool: entrance and two obstructed camera attempts exhausted the three-attempt limit. The pool/foam is visible through the entrance passage, but full-pool appearance is not verified.
- No gameplay, collision, physics, animation, schedules, timing, saves or original assets are changed. Visual checks do not constitute a full gameplay regression run.

## Shoreline foam rework (`fix/shore-foam-waterfalls`)
Problem: the depth-based contact foam read as a wide band of flat grey-white blotches with dark specks (noise gated by a 7-unit band,
capped at 65 % alpha, tinted by a grey sky colour). Evidence: `~/dev/ghm-mods/showcase/water4/shore-*` (left before, right after, 1:1 crops).
- **Line:** a thin, bright foam line right on the waterline (width = 1 unit, or 1.4 pixel footprints so it never aliases), distance to the
  waterline still from thickness / its screen gradient. Bank pixels up to 1.4 units above the plane get the same foam (a meniscus; they used
  to leave dark specks in the line, from depth quantisation). The waterline surges in and out slowly (staggered along the shore).
- **Lace:** broken lacy foam from the committed texture `water/foam_lace_512.png` (unit 39, CC0, made by
  `tools/newdawn/water-flow/gen_foam.py`): R lace filaments, G clumps, B bubbles, A density, sampled with `textureGrad` at 40 units per
  tile, two layers half a cycle apart drifting with the river flow (or a slow wind drift). The lace thins out over 7-15 units
  (squared falloff, threshold rising with distance) and is alpha-faded by footprint.
- **Colour:** HDR white (foam tint x1.2, sunlit above 1); the low-alpha fringe picks up the water's own scatter colour as a cheap
  subsurface tint, the core stays white. Alpha now reaches 0.95 (was 0.65).
- Ocean swash and the river-streak and wave-crest foam are unchanged.
- Views: Ikana river (two angles), Termina pond, Mountain Village spring pool, Great Bay Coast. **Not shown:** Zora Cape (the fixtures
  put the camera in a cave with no water), the cleared Southern Swamp (shore too far for a 1:1 crop) and the Laundry Pool (camera inside
  the wall after three attempts); these use the same pass and were not verified individually.
- GPU, water stage, `PostFx.Debug.Stats=1`: Ikana 1.628 -> 1.672 ms, Termina pond 0.712 -> 0.723 ms (foam off -> on). 0 GL errors.
- Off identity: frozen Stock Pot Inn with the extended AGENT-RULES cvar list and `Water.Foam/Rivers/Waterfalls=0`: this build x2 is
  byte-identical (`e8aaf9fe...`), the older reference build (rivers, pre #108/#109) x2 `705ef49b...`. They differ only on Link's body and a
  halo around him (the merged smooth-characters work); the rest of the frame is identical. A fresh build of current main was not made.
- Limits: the lace is thin at distance (mip-averaged), gently sloping beaches were not found on foot in the fixtures so the fringe is
  seen mostly on banks; the pond rim is a sharp depth edge, so it gets the line and almost no lace.
