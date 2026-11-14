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
