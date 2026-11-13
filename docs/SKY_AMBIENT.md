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
