# World-space global illumination

`gNewDawn.GI.World` (0 off, 1 High, 2 Ultra; **Ultra look 2, High look 1**), `gNewDawn.GI.WorldStrength` (0..3, default 1).
Needs the Modern Lighting Pipeline (`gNewDawn.HDR.Enabled`) and OpenGL 4.4. Menu: Graphics → Advanced → Lighting & Shadows.
Perf overlay: stage 22, "world GI". Renderer: `libultraship/src/fast/backends/gfx_opengl_worldgi.cpp`.

Screen-space GI (`docs/SSGI.md`) only knows what is on screen. This adds bounce light that lives in the world: it does not
change when the camera turns, it comes from surfaces behind the camera, it bounces more than once and walls block it.

## Design

### Scene representation (render-side only)
The renderer never sees a scene, only display lists, so it builds its own copy.
- **Recording.** `Room_Draw` brackets each room's draw with a marker pair (`NEWDAWN_POSTFX_STAGE_GI_ROOM_BEGIN/END`). Inside
  that scope the interpreter also records every solid, depth-writing triangle (`Interpreter::RecordGiTriangle`): world
  position, face normal, a reflectance and a *baked* flag. It records before frustum and back-face rejection and before the
  MM3D room swap. Actors are outside the scope, so they are never recorded. Plain floors and walls that the game draws in a
  texture-edge mode count when their alpha does not come from the texture or the texture is at least 75% opaque; real
  cut-outs (fences, leaves) are left out. Nothing is drawn differently and no game state is read or written.
- **Reflectance.** The colour combiner is evaluated once per triangle on the CPU with the textures replaced by their mean
  colour (about a thousand texels of the loaded texture, cached; all N64 formats and HD RGBA32), the light input set to
  white (lit geometry) or to the vertex colour (unlit geometry), and the real primitive and environment colours. The
  display-space result is used as the reflectance as it is. Decoded to linear, the game's wood and stone come out at
  0.02-0.1 and would bounce next to nothing; their display values sit where real albedos do.
- **Voxels.** The triangles are rasterised into a 3D grid on the GPU (once along each axis at twice the grid resolution,
  `imageStore`): reflectance + occupancy, normal + baked flag, and a 4³ occupancy grid for skipping empty space. **First
  writer wins and a voxel is never overwritten**, so the grid converges and then stays exactly the same whatever subset of
  the room the game submits. The game only submits room meshes that are not wholly behind the camera
  (`docs/CASCADED_SHADOWS.md`), so geometry enters the grid the first time it is submitted and then stays.
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
     reduced boost, plus VFX flames), low-passed and **without** the designed flicker, same falloff, with a shadow ray.
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
   voxel's reflectance (true hue and level); actors use the pixel estimate only. The term fades out in native fog, on
   already bright surfaces and towards the faces of a moving window. Dual-source blending keeps MSAA samples intact.

### Art direction
Three gains decide how much of each source is sent on as bounce (debug cvars `gNewDawn.GI.WorldSunGain/LocalGain/BakedGain`):
daylight 0.5 (the game's ambient already stands for part of it), local lights 3 (a lamp's pool is small and capped, the
bounce has to carry across a room), baked colour 0.6. With world GI on, the screen-space term stays as near-field detail
(contact colour, actors, which the voxels do not contain) at half weight.

## Evidence
(filled in below)
