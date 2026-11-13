# HDR sun diffraction flare (E1)

`gNewDawn.Sky.Flare`: default **0**, New Dawn preset **0** pending the exact-main and Termina sun-facing gates.
It replaces the earlier `GlareFix` setting; old keys are ignored.

The full native sun-flare draw scope is tagged, including all ten ring/circle sprites and the screen wash.
OpenGL suppresses those draws only in an active HDR framebuffer. Native visibility/alpha state updates still
execute unchanged. Custom non-sun flares, LDR frames (including runtime HDR fallback) and screen fades retain
their original draws. No gameplay, camera, animation or environment state is written.

A 48-sample integration across the projected sun disc reads resolved world depth and HDR colour into a 1×1
RGBA16F emitter. Geometry progressively masks its visibility and energy; no CPU depth readback or temporal
history is used. A pixel-filtered six-ray aperture approximation, soft central halo and compact support scatter
that energy locally into HDR, followed by the established bloom and display transform. Optical scatter is
mostly white to avoid an orange veil. There are no offset ghosts, dirt masks, external textures or new dependencies.
The native sun disc and physical atmosphere remain. This is an analytic diffraction approximation, not a full
wave-optics lens simulation.

The exact interpolated projection is captured before the native flare scope. The new `uFlareEmitter` sampler
owns unit **15**, assigned at program creation; a complete transparent dummy remains bound when idle.
Colour/depth reuse existing units 8/9. Stats stage **10** includes depth/colour resolve, emitter and diffraction.

## Validation, 2026-11-13

Main base `51c57581e`, renderer base `6221f142`; Linux Mesa OpenGL, RX 7800 XT, 1920×1080, MSAA 4,
canonical capture tool with `--preset --mods ~/dev/ghm-mods/sets/full`, `MESA_DEBUG=context`.
`cmake --build build -j6` and GLSL 330 validation of both new programs pass. Default shader asset copies are
byte-identical (the initial renderer checkout's CRLF was normalised; the shader archive was repacked).

Coast 17:30 sun-facing image and 1:1 crop inspected: native sprites/wash removed, sun-local starburst retained,
terrain/water colours visible. Stage 10 is **.164 ms GPU** over 300 runs. Zero GL/shader/framebuffer errors.
At the same 17:48 time, the starburst is visible from the original beach position and absent with the sun
behind the pier building (`flare-visible-coast-17.8`, `flare-occluded-coast-2`). An eight-frame Coast turn
was inspected, including 1:1 crops: no obvious discontinuity at sampled angles. HDR-off retains native
sprites/wash and executes no stage 10; the Inn also executes no flare stage. The latest Inn pair has different
idle-animation poses, so it is not a byte-identity proof.
Evidence: `~/dev/ghm-mods/work/glare-fog/flare-first-coast/`, `flare-first-crop.png`;
split: `~/dev/ghm-mods/showcase/glare-fog/flare/coast/` (left native/right modern).

**Exact-main byte identity remains unproven**: the initial frozen Inn comparison differed in sparse render noise
(1,213 changed pixels at matched 14-second timing; another timing changed 22,642). Three attempts were stopped.
The prior implementation's same-build frozen Inn toggle pair was byte-identical; this does not satisfy the main gate.
No markers are emitted with Flare off. **Termina sun-facing glare remains unverified**: three position/framing
failures were stopped. A normal Termina dusk view is included separately; it does not substitute for that gate.

Historical initial attenuation evidence remains in `~/dev/ghm-mods/work/glare-fog/session/e1-*` and showcase `e1/`;
those images retained stock sprites and do not represent the current replacement mode.

Coast sun-facing GPU stage means, ms (300-run averages): AO .411, HDR bloom/display .192,
shadows .279, shafts .335, water .193, opaque copy .135, sky .223, clouds .355, cube .077,
diffraction .164. The turning view diffraction stage averages .166 ms. Fog is disabled for these isolated
E1 measurements. All final flare validation captures have 0 GL/shader/framebuffer errors.

Both final toggles together were also exercised at Coast dusk with zero GL errors (`combined-coast/`);
the combined split is in `~/dev/ghm-mods/showcase/glare-fog/combined/`. No E3 sky replacement is included.
