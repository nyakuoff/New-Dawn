# Sky palette and outdoor ambient

## C1: Termina dawn/dusk palette

Base main `d104c733f`, libultraship `9830bf87` (merged physical sky/clouds and MM3D polish).
The physical sky/clouds now retain the game's dawn/dusk mood as well as scattering structure. After the game's
normal skybox update, the render bridge reads its interpolated primary sky colour. A shared sky/cloud GLSL helper
mixes that palette (80%) with the fog colour (20%), in linear light, normalises its luminance, then biases scattering
hue toward it. Weight peaks at 82% for low positive sun elevation; it fades to zero at noon/night and protects bright
Mie highlights. The scattering gradient, cloud density, daylight clock, celestial draws and horizon fog join remain
unchanged. No new cvar or game/environment writes.

Validation: Linux OpenGL 4.6, RX 7800 XT, 1920x1080, MSAA 4, `capture.py --preset` and full local mods. Both repos
build with `cmake --build build -j6`; `Generate2ShipOtr` passes. The modified sky/cloud programs pass GLSL 330
validation. Default shader asset copies are unchanged and identical. No GL/shader/FBO errors in the captures.
Lead independently reviewed the diff and approved it.

- `/tmp/nd/ambient/c1-{before,after}-{dawn,dusk}-{sun,away}/`: eight matched scene/view comparisons at 06:30 and
  17:30, sun-facing yaw 90/270 and opposite yaw, Termina Field and South Clock Town. All images inspected. Normal
  Clock Town side views have limited sky visibility because of the nearby buildings.
- `~/dev/ghm-mods/showcase/sky-ambient/c1/{dawn,dusk}-{sun,away}/`: before/after split images.
- `/tmp/nd/ambient/c1-sky-frame/`: additional skyward Clock Town check at dusk; warm cloud structure and the native
  giant Moon are visible. The corrected private C-Up fixture holds the down-stick key to look up. This resolves the
  previous milestone's framing issue without changing shipped camera or game code.
- `/tmp/nd/ambient/base/`: exact merged-base executable/archive; `/tmp/nd/ambient/c1-reference/`: C1 reference.

Sky GPU stage 6 remains about 0.222 ms. The skyward check reaches clouds stage 7 at 1.18 ms, below about 1.5 ms
combined sky/cloud budget. Time/day setup in the disposable capture processes uses the private fixture documented
in PHYSICAL_SKY.md; production code reads state only.

## C2: outdoor sky irradiance

`gNewDawn.Sky.Ambient` defaults to 0 and is enabled by the New Dawn preset. The menu toggle is beside Physical
Sky; both physical sky and HDR are required. The existing outdoor classification remains authoritative, including
native fallback for poisoned Swamp, special skies, interiors and dungeons. No game state is changed.

The renderer captures the same physical sky and clouds into a 16x16 RGB16F radiance cube, then uses 64 equal-area
sphere samples to convolve an 8x8 irradiance cube. The lower hemisphere mirrors the sky through a representative
linear ground albedo (grass or stone/earth). HDR world/actor diffuse lighting samples this cube using world normals
from the model transform's inverse transpose. Shadow attenuation uses the same ambient transfer. The baseline
ambient luminance calibrates the result: 65% sky chroma, with directional energy bounded to 95–120% of baseline.
This keeps the existing exposure/readability while adding cool overhead fill at noon and warm fill at dusk.

The cube refreshes every eight rendered frames, immediately on first use, eligibility gaps, scene changes, weather
or control changes, and sun motion exceeding about one degree. Cache validity persists, but readiness resets every
BeginHdr: only an eligible physical-sky draw can enable it for that frame. A Field-to-Inn transition was checked
for retained skylight. Texture unit 12 is dedicated to irradiance; every world program assigns the sampler at creation
and draw time, and a complete neutral cube is bound even with HDR/ambient off. Postfx sampler assignments occur at
link time too. Temporary cube/3D bindings on units 8/9 are restored. GLES retains the existing lighting path.

Validation used Linux OpenGL 4.6/Mesa, RX 7800 XT, 1920x1080, MSAA 4, the same preset and full local mods.
Build/repack, standalone GLSL 330 variants, packaged shader equality and whitespace checks pass. Capture logs with
`MESA_DEBUG=context` and postfx stats contain zero GL/Mesa/shader/FBO errors. Frozen Stock Pot Inn comparisons to
C1 are byte-identical for HDR off/Ambient 0, HDR on/Ambient 0 (repeat), and HDR on/Ambient 1. The first HDR-on/off
comparison had 16 clock-HUD edge pixels differing by one code value; the repeat matched exactly.

Matched GPU measurements include actual world draw batches (`gNewDawn.Materials.Debug.Stats=1`) and postfx stages:

| Measurement | Before | After | Difference |
| --- | ---: | ---: | ---: |
| Field total timed GPU | 2.4085 ms | 2.4590 ms | +0.0505 ms |
| Clock Town total timed GPU | 2.5945 ms | 2.6540 ms | +0.0595 ms |
| Irradiance stage 8, amortized per eligible frame | — | 0.0455 ms | — |

These are averages including cache reuse frames, with no dropped/untimed queries in the final windows. A full cube
regeneration costs approximately 0.36 ms; it is not a 0.046 ms peak. Refreshing every frame failed the performance
gate, so the third attempt used the requested amortization. No layered/geometry shader route was added.

The brightness gate uses fresh matched #38 captures at fixed 08:00, with the same display-luma metric as #38:
mean RGB weighted by .2126/.7152/.0722, cropping rows 60 through height minus 160. An archival snow capture was
14.6% brighter even with C2 disabled, so it was replaced by a matched reference rather than changing fallback lighting.

| Standard point | C2 luma versus matched #38 |
| --- | ---: |
| Deku Palace | +2.0% |
| Great Bay Coast | -3.3% |
| Great Bay Temple | -0.1% |
| Ikana Canyon | +0.0% |
| Mountain Village winter | -0.0% |
| South Clock Town | +3.4% |
| Southern Swamp (poisoned) | +0.1% |
| Stock Pot Inn | +1.7% |
| Stone Tower Temple | -0.1% |
| Termina Field | -2.6% |
| Woodfall Temple | -0.2% |
| Zora Hall | +0.3% |

All 12 pass the maximum 10% brightness-loss gate. Noon Field's shaded stone patch gains 8.0% in blue/red ratio,
with luminance changing -1.2%. The fill change is deliberately subtle; it is most visible on shaded stone/actors.

Evidence (all images inspected):

- `~/dev/ghm-mods/showcase/sky-ambient/c2/{noon,dusk}/`: requested splits for Termina Field, South Clock Town,
  cleared Southern Swamp and Great Bay Coast. Poisoned Swamp retains native sky/ambient fallback.
- `/tmp/nd/ambient/c2-{before,after}-{standard,noon,dusk}/`: matched sources; standard includes all 12 plus cleared Swamp.
- `/tmp/nd/ambient/reference38-standard/`: fresh matched #38 brightness reference.
- `/tmp/nd/ambient/c2-transition-{before,after}/`: outdoor to native warp transition to Inn, including frozen frame 120.
- `/tmp/nd/ambient/c2-timelapse/` and `c2-cloud-drift/`: twelve-frame sequences, no visible irradiance stepping.
  A fixed wall patch's maximum frame-mean channel step was 0.067 code values.
- `/tmp/nd/ambient/c2-perf-{before,cache}/logs/`: matched GPU timing logs.
- `/tmp/nd/ambient/sampler-{ldr-off,hdr-off,hdr-on}-{reference,new}/` and `sampler-hdr-off-repeat/`: sampler/off checks.
- C1's additional skyward comparisons are in `~/dev/ghm-mods/showcase/sky-ambient/c1/sky-{dawn,dusk}-{sun,away}/`.

Scope limits: diffuse irradiance only; optional prefiltered specular/water IBL is deferred. Experimental MM3D room
shaders retain their existing lighting. The low-resolution cube is a broad ambient approximation, without local
visibility/occlusion beyond existing AO and shadows. No gameplay changes or shipped capture fixtures.
