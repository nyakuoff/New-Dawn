# Ambient particles (A1)

Base: game `dd4c4a108`, renderer `d2ae82aa`. OpenGL desktop only; HDR required.

`gNewDawn.Atmos.Particles` defaults to **0** and is **1 in the New Dawn preset**. The exact-main
frozen-Inn identity gate passes; see the integration update below. The New Dawn menu includes a toggle and `gNewDawn.Atmos.Strength`
(default 1, range 0–2; zero skips the pass). `gNewDawn.Atmos.SpritePath` and `.LeafPath` accept absolute
paths or paths relative to the application directories; restart after changing a texture path. Packaged defaults
are `assets/newdawn/atmos/{dust_64,leaf_64}.png`. The build copies these small CC0 files beside the executable.

GPU-instanced, world-seeded billboards occupy a camera-local periodic volume. A spherical outer fade reaches
zero before a particle changes periodic image; turning the camera does not move the volume's world seeds.
Monotonic renderer time drives drift and gentle turbulence, independently of game time and RNG. Sunlit dust
samples the existing sun shadow map; without that map it receives a dim fallback. Depth-softening prevents hard
intersections, with no depth writes. The pass runs after opaque AO/shadows/fog and the opaque water reflection
copy, before native translucency and HDR bloom/display. Leaves use premultiplied blending; dust/fireflies add
in-scattering. Texture units 16–19 belong to the new program, assigned at creation with complete textures bound;
neutral depth textures remain bound when idle. Shader/asset failure skips only this optional pass.

The read-only bridge selects courtyard dust in Clock Town, Coast and Ikana; pollen in Field; pollen/leaves in
Swamp, Woodfall and Woods; fireflies after dusk in Field/Swamp/Woods. Inn dust receives existing nearby point
lights only. The nearest eight native point lights are selected. Mountain Village is deliberately outside the
ambient-particle table; snow remains native pending A2. No actors, game/environment state, animation, collision,
save data or gameplay RNG are modified. Native render-side effects retain their original updates and draws.

Assets: Kenney Particle Pack 1.1 `circle_05.png`, CC0, downsampled to 64px; cemkalyoncu's OpenGameArt Leaf Pack
`eastern redbud.svg`, CC0, rasterized to 64px. Attribution/source manifests and the Kenney license are in
`libultraship/assets/atmos/`; full local sources are in `~/dev/ghm-mods/assets/{kenney-particle-pack,oga-leaf-pack}/`.

## Evidence, 2026-11-13

Linux Mesa OpenGL 4.6, Radeon RX 7800 XT, 1920×1080, MSAA 4, New Dawn preset + full local mod set.
Verified build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`, `cmake --build build -j6`,
`cmake --build build --target Generate2ShipOtr -j6`. Both particle GLSL 330 stages pass `glslangValidator`.
Whitespace checks pass. Independent cavecrew review found and prompted fixes for reflection-copy ordering,
absolute texture paths and PNG build dependencies.

Canonical `python3 tools/newdawn/capture.py --bin-dir build/mm --preset --mods /home/loganp/dev/ghm-mods/sets/full ...`
launches every capture. The private `~/dev/ghm-mods/work/atmos/capture_fixture.py` companion sets time/day and the
existing developer frame advance in disposable children only. Scratch/captures live on disk under that directory.

Visually inspected before/after matrix and 1:1 crops: Field 08:00/12:00/17:30/22:00, cleared Southern Swamp day/night,
Woodfall exterior, Woods of Mystery, South Clock Town dry and day-2 active rain, winter Mountain Village, Inn.
Sunlit motes and nighttime fireflies are visible. Native weather/geometry/HUD remain present. The fixed Inn view
has no demonstrated local-light dust; its on/off difference is tiny and cannot establish the requested lamp-side
interior effect. Torch-specific embers are not implemented: a point light alone does not reliably identify fire.
Leaf sprites are visible in Swamp, Woodfall and an additional opposite-facing Woods view. The initial Woods framing faces nearby walls. An eight-frame C-Up turning burst and its 1:1 crops show persistent soft motes across the sampled angles, without obvious seams or flicker; this is coarse motion evidence, not a continuous high-frame-rate proof. Evidence: `a1-turn/`, `a1-motion.jpg`, `a1-motion-crops.png`, `a1-woods-path/`.

GPU **stage 11** (`gNewDawn.PostFx.Debug.Stats=1`) averages **0.003–0.009 ms**, two 300-run windows per eligible
scene. Field day .008–.009, night .007; Swamp .007–.009; Town .007–.008; Inn .006–.007 ms.
This times the new stage; if AO/shadows/water are all disabled, an additional resolved-depth copy is required
and is not included in stage 11. Mountain Village runs no particle stage. Final matrix logs: zero GL/Mesa,
shader compile/link or framebuffer errors with `MESA_DEBUG=context`. Raw measurements: `a1-gpu.json`.

Evidence: `~/dev/ghm-mods/work/atmos/a1-{field-*,southern_swamp_cleared,woodfall_exterior,woods_of_mystery,
south_clock_town,swamp-night,clock-rain,on-inn}/`, contact sheets `a1-after-matrix.jpg`, `a1-after-crops.png`;
matched splits in `~/dev/ghm-mods/showcase/atmos/a1/`.

## Original fixture attempts and limits

**Historical failed fixture (superseded by the integration update below).** The exact base was rebuilt from unchanged sources and
archived as `main-reference/`; an older existing executable was rejected as stale. The first frozen-Inn comparison
differed at 23,756/2,073,600 pixels (mostly one RGB code value; 61 pixels exceeded two values; RMSE .129/255).
Subsequent attempts alternated between the same two stable image hashes across both binaries. A private debugger
fixture fixing the renderer noise counter did not resolve it. The three-attempt limit was reached; investigation
stopped. Interpolation phase is a remaining hypothesis, not a verified explanation. No shipped freeze/debug code
was added. Fixing interpolation to 20 FPS subsequently resolved this gate.

Point lights lack local shadow visibility. Sun visibility inherits existing camera-culled shadow casters. There is
no particle collision or ground settling; depth rejects hidden particles, rather than simulating their interaction
with geometry. Static captures and coarse camera bursts cannot prove high-frame-rate motion safety.

## Integration update, 2026-11-14

Visibility was retuned before this handoff: sparse warm-white motes with varied brightness, slow
meandering hover, near-camera softness and twinkle; yellow-green fireflies blink with soft halos.
Full frames and 1:1 crops through preset fog: `~/dev/ghm-mods/showcase/atmos/a1v2/`.
With `Atmos.Wind=1`, cloud and particle drift now share the integrated gust from `newdawn_wind.h`;
with Wind off, the original particle clock/drift path remains active. No additional assets or samplers.

Exact-main off identity: main `f7f8b1f1c` / renderer `e4aba70e`, 12:00 Inn, game frame 30,
`gMatchRefreshRate=0`, `gInterpolationFPS=20`, preset and full mods. Two captures per binary:
all four PNG SHA-256 `46ad33f92c94c188f017961d297107a7295ca957e83e1ce39ec54ea04103329d`.
The earlier fixture had left interpolation at the display rate. Evidence and build details:
`ATMOS_WIND.md`, `~/dev/ghm-mods/showcase/atmos/final/identity-*.png`.

Final integration: ambient-particle stage 11 **0.006–0.019 ms GPU**, including night rain,
night fireflies and the close grass view with weather and wind also enabled. Near sprites
are deliberately blurred and can read as large bokeh. The shared-wind burst, final preset
proof and firefly frame are in `~/dev/ghm-mods/showcase/atmos/final/`. Interior near-light dust
and torch-specific embers remain unverified/unimplemented as described above. No game state, animation, collision, RNG or save behavior is modified.
