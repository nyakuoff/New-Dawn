# Textured HDR lens flare (F1)

`gNewDawn.Sky.Flare`: default **0**, New Dawn preset **1**. Requires active HDR and the local VFX pack.
The rejected procedural diffraction star is removed. All flare shapes now sample downloaded glow, soft bloom,
hexagon, iris and ring PNGs. Small, tinted ghosts follow the sun–screen-centre axis. Their separation fades
near screen centre; the entire flare fades at frame edges. There are no generated spikes or full-screen wash.

The existing 48-sample sun-disc integration supplies HDR energy and geometry visibility. It now also samples
this frame's rendered cloud opacity toward the sun, including the cloud/sky strength blend. Opaque clouds
suppress the flare. Native sun flare sprites/wash stay suppressed only in HDR; all environment-state updates,
non-sun flares and LDR/runtime fallback behavior remain native. No gameplay/animation/camera state is written.

## Downloaded assets and installation

Source: [Lens-Flares and Particles, hackcraft.de](https://opengameart.org/content/lens-flares-and-particles).
[Archive](https://opengameart.org/sites/default/files/flare_20120324.zip), downloaded 2026-11-13.
The archive's `flare/LICENSE` says **CC0 – Public Domain Donation by hackcraft.de**; the OGA listing also
advertises CC-BY 3.0. The local manifest preserves attribution and both notices. No art is committed.

Installed pack: `~/dev/ghm-mods/assets/oga-lens-flares/`; provenance, file hashes and licence information:
`MANIFEST.md`. Runtime files under its `flare/`: `star.png` (soft bloom, despite its filename), `extend.png`
(halo), `hexangle.png`, `iris.png`, `sun.png`, `ring.png`. Original downloads are retained unchanged.
Kenney's CC0 particle pack was also downloaded/inspected, with its own manifest; it is not loaded.

`gNewDawn.Assets.Path` is the directory **containing** `oga-lens-flares`, default `assets/newdawn` relative
to the executable's working directory. This machine's `build/mm/assets/newdawn` links to
`~/dev/ghm-mods/assets`. Alternatively set the cvar to `/home/loganp/dev/ghm-mods/assets` in the config.
For another installation, extract the linked archive into `<asset-root>/oga-lens-flares/` and preserve
its LICENSE and attribution. Missing/invalid files log once per path and leave only the normal HDR sun/bloom.
Changing the path reloads the pack. Six unchanged masks upload into a 256² texture array with mips;
128² hexagon art is centred without resampling and uses a matching UV rectangle. About 2 MiB VRAM.

Sampler assignments happen at program creation: emitter **15**, sprite array **16**, cloud opacity **17**.
Complete neutral textures remain bound when idle or disabled. Colour/depth reuse 8/9. GL 3.3 guarantees
48 combined texture units; the shader uses only two fragment samplers in its apply pass. Stats stage **10**
includes depth/colour resolve, emitter and textured flare. The default shader files were not edited.

## Validation

Base main `dd4c4a108`, renderer `d2ae82aa`; Linux Mesa OpenGL, RX 7800 XT, 1920×1080, MSAA 4,
canonical `capture.py --bin-dir build/mm --preset --mods /home/loganp/dev/ghm-mods/sets/full`.
`MESA_DEBUG=context`, `cmake --build build -j6`, both new GLSL 330 programs pass validation.

- Coast and Termina facing the sun at 12:00 and 17:30: all images inspected. No procedural spikes.
  The private camera fixture supplies a fixed view; Termina dusk's second angle avoids the foreground pillar.
- Preset clouded views attenuate ghosts as intended. A separate clear-sun Coast capture (`Sky.Clouds=0`)
  visibly shows the downloaded iris/hexagon/ring ghosts and local glow, with no frame-wide veil.
- Frozen Stock Pot Inn frame 120, Flare=0/Fog=0: **PNG byte-identical to exact main**.
  Both SHA-256: `c6b90c3e69c85f732f5eaebf40f409007399f0316192e5f386030cb078e54a82`.
- Missing-pack and clear-sky geometry-occlusion checks also have zero GL errors. Missing art falls back
  to normal sun/bloom; an occluded sun produces no ghosts. Independent F1 diff review found no issues.
- Validated captures: zero GL/shader/framebuffer errors; stage 10 Coast dusk **0.174–0.175 ms GPU** (300 runs).

Raw captures/logs: `~/dev/ghm-mods/work/flare-textures/{base-*,f1-*}`.
Splits made with `/tmp/nd/showcase/split.py`: `~/dev/ghm-mods/showcase/flare-textures/f1/`.
Left is the merged procedural flare; right is the textured replacement, same preset/mods. Clear-sun control
is labelled separately; it does not isolate the cloud change against those before images.
