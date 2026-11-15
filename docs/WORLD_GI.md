# World-space global illumination

`gNewDawn.GI.World` (0 off, 1 High, 2 Ultra; **Ultra look 2, High look 1**), `gNewDawn.GI.WorldStrength` (0..3, default 1).
Needs the Modern Lighting Pipeline (`gNewDawn.HDR.Enabled`) and OpenGL 4.4. Menu: Graphics → Advanced → Lighting & Shadows.
Perf overlay: stage 25, "world GI". Renderer: `libultraship/src/fast/backends/gfx_opengl_worldgi.cpp`.

Screen-space GI (`docs/SSGI.md`) only knows what is on screen. This adds bounce light that lives in the world: it does not
change when the camera turns, it comes from surfaces behind the camera, it bounces more than once and walls block it.

## Design

### Scene representation (render-side only)
The renderer never sees a scene, only display lists, so it builds its own copy.
- **Recording.** `Room_Draw` brackets each room's draw with a marker pair (`NEWDAWN_POSTFX_STAGE_GI_ROOM_BEGIN/END`). Inside
  that scope the interpreter also records every solid, depth-writing triangle (`Interpreter::RecordGiTriangle`): world
  position, face normal, a reflectance and a *baked* flag. It records before frustum and back-face rejection and before the
  MM3D room swap. Actors are outside the scope, so they are never recorded. At the end of the scope the opaque room
  entries the game culled this frame (`Room_DrawCullable` skips meshes wholly behind the camera or beyond zFar) are handed
  over as a record-only display list, the mechanism #102 added for shadow casters, with a bit that marks it as world
  GI's: it runs only on recording frames, draws nothing, records no shadow casters and restores every piece of render
  state. So the grid holds the whole loaded room at once, wherever the camera points. Plain floors and walls that the game draws in a
  texture-edge mode count when their alpha does not come from the texture or the texture is at least 75% opaque; real
  cut-outs (fences, leaves) are left out. Nothing is drawn differently and no game state is read or written.
- **Reflectance.** The colour combiner is evaluated once per triangle on the CPU with the textures replaced by their mean
  colour (about a thousand texels of the loaded texture, cached; all N64 formats and HD RGBA32), the light input set to
  white (lit geometry) or to the vertex colour (unlit geometry), and the real primitive and environment colours. The
  display-space result is used as the reflectance as it is. Decoded to linear, the game's wood and stone come out at
  0.02-0.1 and would bounce next to nothing; their display values sit where real albedos do.
- **Voxels.** The triangles are rasterised into a 3D grid on the GPU (once along each axis at twice the grid resolution,
  `imageStore`): reflectance + occupancy, normal + baked flag, and a 4³ occupancy grid for skipping empty space. **First
  writer wins and a voxel is never overwritten**, so the grid converges and then stays exactly the same from frame to
  frame (room display lists pick level-of-detail branches by depth, so the submitted triangle set does change).
- **Window.** The grid is a window into the world whose addressing wraps (world cell `c` lives at texel `c & (size - 1)`).
  If the scene's collision bounds fit (voxel size up to 24 units on Ultra, 40 on High), the window is the whole scene and
  never moves. Larger scenes (Termina Field, Great Bay Coast) get a window that follows the camera in steps of 16 voxels;
  a move empties only the slabs that entered, everything else keeps its data. The grid is emptied on a scene change. It is
  not emptied on a room change: rooms of one scene share its coordinates, so a visited room stays lit correctly.
- **Recording cadence.** Every frame for the first 40 frames of a scene and for 3 frames after a window move, then every
  15th frame.

### Lighting
All of it is view independent.
1. **Radiance volume** (a few z-slices per frame, the whole volume every 16 frames). Every occupied voxel gets its outgoing
   radiance:
   - *lit geometry* (outdoor rooms use the game's lights): the sun or the Moon (same fades as the shadows) with a shadow ray
     walked through the voxels, the sky (the sky ambient cube of #43/#45, or the ambient colour) times the sky visibility
     the probes measured, the local lights, and the probes' own irradiance (this is the multi-bounce feedback);
   - *baked geometry* (interiors and dungeons are unlit and vertex-coloured, the game never lights them): what the game
     displays, the painted colour, as an emitter, plus the local lights and the bounce it receives.
   - *local lights*: the list the local-light pass built this frame (game point lights incl. Tatl's tinted light at its
     reduced boost, VFX flames, and the lights of the emissive fixtures of #103), low-passed and **without** the designed
     flicker, same falloff, with a shadow ray.
2. **Probes.** A 32×16×32 window of probes, one every 8 voxels (4 on High), each a 6×6 octahedral irradiance tile (RGB
   bounce, A sky visibility) and a 6×6 tile of distance moments. Each frame a block of probes (1024 on Ultra, 512 on High)
   traces rays (128 / 64, a new random rotation of a spherical Fibonacci set every update) through the voxels with a
   two-level walk, reads the radiance volume at the hit and blends into its tiles with hysteresis 0.88 (the first sweeps
   after a scene change average instead, and run with a 4× budget for 40 frames). Rays that hit a back face return no
   light and a shortened distance, which keeps probes inside or behind geometry from lighting anything.
3. **Apply** (at the opaque-end marker, after the local lights so their darkening does not swallow the bounce they cause).
   Per pixel, from depth and the depth-derived normal: the 8 surrounding probes weighted by trilinear, a wrap term and the
   Chebyshev visibility test on the distance moments. The receiving albedo is the geometric mean of the pixel's own estimate
   (square root of the lit colour, the local-light pass's convention: keeps texture detail and painted darks) and the
   voxel's reflectance (true hue and level); actors use the pixel estimate only. Grass tufts (their own stencil class)
   are drawn separately with the ground's upward normal: thin blades have no usable depth normal. The term fades out in
   native fog, on already bright surfaces and towards the faces of a moving window. Dual-source blending keeps MSAA
   samples intact.

### Art direction
Three gains decide how much of each source is sent on as bounce (debug cvars `gNewDawn.GI.WorldSunGain/LocalGain/BakedGain`):
daylight 0.5 (the game's ambient already stands for part of it), local lights 3 (a lamp's pool is small and capped, the
bounce has to carry across a room), baked colour 0.6. **Deep shade stays deep:** in daylight the bounce may raise a pixel by
at most about 60% of its own level, so dark rock and painted blacks are tinted, not greyed (#81); by lamplight (night,
interiors) the limit is 3×, so a torch can fill a dark corner.

**SSGI: kept, at half weight.** Measured on the eight scenes below, world GI alone gives the large-scale fill and the
off-screen and multi-bounce light; it cannot give contact-scale colour (probes are 84-192 units apart) or bounce from
actors (not in the voxels), which is what SSGI does well. With `gNewDawn.GI.World` on, SSGI's strength is halved so the two
do not add the same light twice. Ultra runs both; High runs world GI only (as before, High has no SSGI).

## Evidence
RX 7800 XT (radeonsi), OpenGL 4.6, 1920×1080, MSAA 4, TAA, Ultra look, community mod set, merged tree (superproject on
`ba7b72cfc`, libultraship on `b714bc69`). Game logic frozen at frame 30 unless noted. Scripts and raw captures:
`~/dev/ghm-mods/work/world-gi/` (`final2/`); images: `~/dev/ghm-mods/showcase/world-gi/`.

### Before / after (display luma, rows 60..920: mean / p5 / p50 / p95)
| scene | GI off | SSGI (before) | world GI only | world + SSGI (Ultra) |
| --- | --- | --- | --- | --- |
| South Clock Town 12:00 | 87.3 / 26.0 / 83.8 / 177.0 | 90.8 (+4.0%) | 92.3 / 30.0 / 89.0 / 182.7 (+5.8%) | 94.5 (+8.3%) |
| South Clock Town 00:00, torch | 65.3 / 12.7 / 58.0 / 150.1 | 63.4 (-2.9%) | 69.8 / 17.4 / 62.0 / 154.0 (+6.9%) | 69.8 (+6.8%) |
| Stock Pot Inn 12:00 | 42.1 / 11.1 / 28.8 / 101.5 | 43.8 (+4.1%) | 46.3 / 14.8 / 33.0 / 104.5 (+10.2%) | 48.1 (+14.4%) |
| Stock Pot Inn 00:00, lamp alcove | 26.1 / 9.9 / 19.1 / 67.7 | 29.1 (+11.4%) | 27.6 / 11.1 / 20.7 / 69.7 (+5.8%) | 30.1 (+15.1%) |
| Clock Tower interior (opening cutscene) | 24.4 / 0.0 / 11.9 / 132.6 | 25.3 (+3.7%) | 24.8 / 0.0 / 12.4 / 133.5 (+1.7%) | 25.4 (+4.0%) |
| Woodfall Temple | 33.3 / 11.4 / 18.0 / 96.1 | 33.4 (+0.6%) | 33.8 / 11.7 / 19.0 / 96.7 (+1.8%) | 34.0 (+2.4%) |
| Termina Field 17:30 | 51.5 / 12.3 / 35.9 / 122.4 | 52.3 (+1.7%) | 52.8 / 14.1 / 37.1 / 123.1 (+2.6%) | 53.3 (+3.5%) |
| Great Bay Coast 12:00 | 117.8 / 22.2 / 112.4 / 202.9 | 119.3 (+1.3%) | 121.7 / 28.5 / 116.7 / 204.2 (+3.3%) | 122.7 (+4.2%) |

The torch scene varies by a few percent between runs (the flame animates on the wall clock), which is why its SSGI figure is
negative. Images per scene: `<scene>_{off,ssgi,world,ultra,bounce_x4}.png`, `<scene>_sheet.png` (all four),
`<scene>_split.png` (left off, right Ultra), `crops/<scene>_crop.png` (1:1, off | SSGI | Ultra). What they show:
- **South Clock Town noon**: the shaded paving and the guard's side pick up warm light from the sunlit plaza and a red
  cast from the plaster wall; black cloth stays black. 1:1 crop: smooth, texture detail intact, no blocks or noise.
- **South Clock Town night**: the torch warms the tent canvas, the paving and the wall to its right well past the
  screen-space pool of the local-light pass.
- **Stock Pot Inn**: by night the lamp alcove spills warm light onto the hall floor in front of it; by day the bright
  doorway and the planters tint what is near them. The dark hall walls stay dark.
- **Woodfall Temple, Clock Tower**: small change; where no light reaches nothing is added.
- **Termina Field dusk, Great Bay Coast**: shaded grass and cliff faces take a little fill from the lit ground; before
  the daylight limit the Coast's cliffs went grey (p5 22 to 44), with it p5 is 28.5.

### Stability
- **Rotation** (`rotation_sct_*.png`: a 5.5 s first-person turn in South Clock Town, 10 shots): the
  bounce-only view of world GI is a continuous field in every shot; the SSGI bounce-only view of the same turn is patches
  beside whatever lit surface happens to be on screen. Numerically (`gNewDawn.GI.WorldLog=1`, the mean of the whole probe
  field every 30 frames): 0.01072 before the turn, 0.01079 after it (+0.6%, the same slow convergence slope as the 450
  frames before the turn), while the number of submitted room triangles went 885, 901, 808, 901. The probes do not depend
  on the view.
- **Static camera, frozen game** (8 shots 0.5 s apart, wall-clock animation off; per-pixel standard deviation in display
  levels): South Clock Town off 0.019, SSGI 0.223, world GI 0.069; Inn lamp alcove off 0.697, SSGI 0.796, world GI
  0.685. The probe updates add less than a tenth of a display level of temporal noise, a third of SSGI's.
- **Window move** (`move_field*.png`: an 11 s run across Termina Field): the window moved four times (16 voxels = 384
  units each); no pop in the bounce-only burst, GPU 1.30-1.32 ms, 0 GL errors.

### Light leaks
- `leak_sct_stall.png` (South Clock Town noon, under the thatched stall beside the sunlit plaza): the voxel radiance view
  shows the stall floor unlit by the sun (the roof blocks the voxel shadow ray) next to the lit plaza; the probes' sky
  visibility is low under the roof and high outside; the bounce inside comes in through the open front.
- `leak_inn_alcove.png` (Inn, night): the lamp's bounce is in the alcove and on the floor in front of its opening; the
  hall walls beside the alcove stay dark in the bounce-only view.
- By construction: interiors have no sun or sky term at all, and a ray that reaches a surface from behind returns no
  light.

### Performance
GPU time of the whole stage (voxelise, relight, trace, blend, apply), 300-frame averages, steady / first 300 frames (which
include the 40 warm-up frames at 4× budget):

| scene | Ultra | High |
| --- | --- | --- |
| South Clock Town | 1.14 / 1.36 ms | 0.64 / 0.69 ms |
| Stock Pot Inn | 0.96 / 1.11 ms | 0.60 / 0.63 ms |
| Clock Tower interior | 0.86 / 1.02 ms | |
| Woodfall Temple | 1.01 / 1.18 ms | |
| Termina Field | 1.46 / 1.83 ms | 0.65 / 0.73 ms |
| Great Bay Coast | 1.27 / 1.55 ms | |

SSGI beside it on Ultra: 0.28-0.35 ms, so GI as a whole is 1.2-1.8 ms (target: about 2 ms). In the performance overlay
(`perf_overlay.png`, detailed view) "world GI" is listed at 1.16 ms in South Clock Town, the largest single stage of a
6.1 ms GPU frame.
- **Build on a scene change**: voxelising the recorded room (487 to 2,931 triangles in these scenes, three rasterisations)
  takes 0.16-0.71 ms wall clock with `glFinish` on both sides; clearing the grids is six `glClearTexImage` calls. The
  probes converge during the 40 warm-up frames (about one sweep every 4 frames), mostly under the scene transition's fade.
- **Memory** (VRAM): Ultra 118 MB (three 256×128×256 4-byte volumes 100.7 MB, probe atlases 12.6 MB, ray buffer 4.2 MB,
  coarse grid 0.1 MB); High 26 MB. CPU side: the recorded triangles of one frame (120 bytes each) and a few hundred cached
  texture averages.
- **Off identity**: frozen Stock Pot Inn (12:00, frame 30, `gMatchRefreshRate=0`, `gInterpolationFPS=20`, the full
  deterministic-off list of the agent rules incl. TAA, fog, shafts, physical sky, flare, local lights and water),
  `GI.World=0` and `GI.Enabled=0`: two runs of main (`ba7b72cfc`, the play build) and two of this branch have the same
  PNG SHA-256 `705ef49b…`. With the feature off no marker is emitted and none of the code runs.
- **0 GL errors** under `MESA_DEBUG=context` with stats on in every run above (40 matrix captures, identity, bursts).

## Known artefacts and limits
- **Actors are not in the grid.** They receive bounce but neither block nor cast it: stalls, doors, moving platforms,
  the boats at the Coast. SSGI covers their near-field bounce on Ultra.
- **Rooms that are not loaded** are not in the grid, and a room's voxels are those of the first triangles that reached
  each cell: one reflectance per triangle (texture mean × combiner), captured as it was when first recorded (a texture
  still loading asynchronously, a primitive colour that changes later). It is refreshed only when the scene changes.
- **Resolution.** Voxels are 10-24 units on Ultra (20-40 on High), probes 84-192 units apart. Openings and gaps smaller than
  a voxel are closed or missed; bounce is low-frequency. The sun shadow in the radiance volume is voxel-sized and ignores
  actors and cut-outs (a tree's crown does not shade the bounce source).
- **Probe visibility** is the usual moment test: next to thin walls it leaves soft darker or lighter patches (visible in
  the sky-visibility debug view as blotches on the stall counter and the far wall). This is why the sky-occlusion option
  (`gNewDawn.GI.WorldOcclusion`, darkens enclosed places) exists but is off.
- **Big scenes** (voxel size at the cap): the window is 6,144 units wide on Ultra; the bounce fades out over its last two
  probe cells and at the scene's bounds (the gate tunnel of South Clock Town gets none). After a move the slab that
  entered converges over about a second, at the far edge.
- **Response time.** The radiance volume is relit every 16 frames and the probes blend with hysteresis 0.88 per sweep: a
  light that changes abruptly takes one to two seconds to show in the bounce. Deliberate (no flicker), but a torch being
  lit fills the room late.
- **Local-light bounce needs `gNewDawn.Lighting.Local`**: the light list comes from that pass. Without it only sun, sky
  and baked colour are injected.
- **Art-directed, not physical**: display-space reflectance, per-source gains and the daylight limit are choices for
  this game's dark textures and the #81 direction.
- **Not verified**: pause and pictograph, cutscenes other than the Clock Tower opening, MM3D rooms (the N64 room is
  recorded by construction, untested), mirrored culling, window moves along the vertical axis, GPUs other than this one.
  OpenGL only, 4.4 or newer (the feature logs once and stays off otherwise).

## What remains
- A second, finer window around the camera for the big outdoor scenes (probes every 192 units are coarse next to Link).
- Static actors (stalls, doors, platforms) splatted into a separate layer that is rebuilt each frame.
- An albedo attachment in the world pass, so the receiving surface uses its real texel instead of an estimate.
- Emissive surfaces as area emitters in the voxels themselves (today they reach GI only through their matching lights).
- Sky occlusion once the probe visibility is cleaner (probe relocation off surfaces).
- Glossy reflections from the same voxels.

## Debug
`gNewDawn.GI.WorldDebug`: 1 bounce only ×4, 2 voxel reflectance, 3 voxel radiance, 4 sky visibility, 5 voxel normals,
6 raw probe irradiance, 8 occupancy / baked flag / window fade. `gNewDawn.GI.WorldLog=1`: every distinct room draw state
with the reflectance it was given, and the probe-field mean. `gNewDawn.GI.WorldFreeze=1`: stop updating, apply only.
Tuning: `WorldSunGain`, `WorldLocalGain`, `WorldBakedGain`, `WorldBounce`, `WorldHysteresis`, `WorldReceive`.
