# Interior polish (roadmap item 7)

Render-only, HDR only. Branch `feature/interior-polish` (local, not pushed). The emissive glow is on in Ultra and High; the **added windows (and so their shafts, pools and beam dust) are off in every look** until the maintainer decides on them: turn on Advanced > Interiors > Window Light.

| Cvar | Default | What |
| --- | --- | --- |
| `gNewDawn.Interior.Windows` | 0 (all looks) | Windows, light shafts, pools of light, dust in the beams |
| `gNewDawn.Interior.Strength` | 1 (0-3) | Light multiplier |
| `gNewDawn.Interior.Motes` | 1 | Dust in the beams (needs `Atmos.Particles`) |
| `gNewDawn.Interior.Pools` / `.Shafts` / `.Panes` | 1 | Developer toggles for the three parts of the window pass |
| `gNewDawn.Interior.Emissive` | 0 (preset 1) | Glowing clock emblem and signs, with a matching local light |
| `gNewDawn.Interior.EmissiveStrength` | 1 (0-3) | Emission multiplier |
| `gNewDawn.Interior.Debug`, `.Probe`, `.TestWindow` | 0 | Developer aids (below) |

All of them are in the New Dawn menu (Lighting & Shadows, "Interiors").

## What the survey found
Majora's Mask interiors have almost no windows. The Stock Pot Inn lobby, the Mayor's Residence hall, the Milk Bar, the Swordsman's
School, the Bomb Shop, the Trading Post and the Post Office are closed boxes with a door (a few doors carry stained glass). The texture
scan found cut-out "window" textures only in side rooms, and the one the Inn uses (`Z2_YADOYA_room_00Tex_0147E0`) is a 9-unit fixture.
So detecting openings from the scene gives almost nothing. **The remaster adds windows**: a per-scene table
(`sNewDawnWindows`, NewDawnPostFx.cpp) of render-only openings placed on real wall planes. Each is a visible pane with a dark frame
and mullions, drawn on the wall, plus the light it lets in. No geometry, collision or actor data changes. The table was filled
with the wall positions the `gNewDawn.Interior.Probe` log reports (world position under a 9 x 5 grid of screen points), then checked on screen.

Rooms covered: Stock Pot Inn lobby (2), Mayor's Residence (2), Milk Bar (2), Swordsman's School (4), Bomb Shop (2), Trading Post (2),
Post Office (2). **Not covered:** the Clock Tower interior (only reachable in a cutscene camera, so the walls could not be probed),
the Curiosity Shop and Honey & Darling (probe frames did not follow the camera), the Potion Shop, the Lottery Shop, the Astral
Observatory. Windows at the front of Clock Town buildings (glowing windows seen from outside) are **not implemented**: the town
has no window geometry either and no valid probe position was found for the facades.

The interpreter still records real openings: `newdawn_interior_textures.inc` lists textures by resource basename (`kind 1` = window
with inset and pane counts), merged per frame into rectangles. The list is empty of windows for now because no verified window texture
exists in the scenes above; the path is wired and goes through the same pass.

## How it works
- **Light** (`NewDawnFillInteriorParams`): clock-driven. Sun elevation `sin = 0.24 + 0.62 * sin(pi * (h - 6) / 12)` (low near dawn and
  dusk, high at noon); colour from warm at dawn/dusk to near-white at noon, a cool moon at 7% intensity at night; rain and snow cut the
  daylight by up to 80%. The horizontal direction follows each opening's own normal (interior scenes have no compass).
  Interiors only (`NewDawnSceneIsIndoors`).
- **Pass** (`PostFxGL::RunInteriorLight`, stage 22, after the local lights): one full-screen pass, dual-source blended
  (`result = shaft + framebuffer * boost`). *Pool*: each surface point is projected back along the beam onto the opening; a
  soft-edged (penumbra grows with distance), pane-barred mask, a 10-step screen-space depth march toward the opening for occlusion
  (furniture and Link shade it) and a facing term light it; the colour lifts the painted albedo the way the local lights do, so darks stay
  dark. *Shaft*: the view ray is intersected analytically with each beam prism (slab test in window/beam coordinates), 6 jittered
  samples, Henyey-Greenstein forward scattering (stronger when looking toward the window), clamped at the first surface.
  *Pane*: a depth-tested quad with a warm wood frame and mullions; the glass is frosted (low-frequency noise), shows a horizon-to-zenith
  gradient and darkens toward the frame, lit by the same light (night is nearly black blue). It does not sample the sky cube: interiors
  have no sky-ambient/reflection cube (the physical-sky marker never fires there), so the gradient is procedural.
- **Dust** (only exists where a window beam does, so it follows the Window Light toggle): the existing ambient-particle pass (CC0 Kenney `circle_05` sprite, no new asset). In interiors with windows it draws 2048
  instead of 1024 motes; every second one spawns inside a beam prism, drifts slowly and is lit only while inside it (membership matches
  the pass). Others keep the old near-lamp behaviour.
- **Emissive** (`RunEmissive`, stage 23): draws of classified textures write stencil 4 (night glass) or 8 (lamps, signs) at the opaque
  stage; a full-screen draw per class adds HDR emission from the painted colour (pattern preserved), so bloom picks it up. The local-light
  darkening skips these pixels. Entries can also feed an `emissiveLights` slot (one local light per fixture, night-only for glass).
  The pass is skipped on frames with no emissive draw. Listed: the Clock Tower emblem/face textures and Mayor/Inn wall emblem (after dusk),
  the lottery shop sign.
- **Ambient occlusion in corners**: checked with AO on/off at the Mayor's hall corner and furniture bases (`ao.png` in the work
  directory). SSAO (strength 1.2, radius 40) already gives visible contact darkening at wall/floor junctions and under pots; GI adds
  bounce. No change made, blacks untouched.

## Evidence (RX 7800 XT, 1080p, MSAA 4, preset + community mods, frozen at game frame 30)
Images in `~/dev/ghm-mods/showcase/interior-polish/` (`*-off.png`, `*-on.png`, `*-split.png` = left half off / right half on, `*-crop1to1.png`):
Stock Pot Inn day and night, Mayor's Residence, Milk Bar day and night, Swordsman's School, Bomb Shop, Post Office, South Clock Town
at night (emblem lit).
- GPU (`PostFx.Debug.Stats`, 300-run averages): window pass **0.32-0.43 ms** (Inn 0.32, Mayor 0.43, Milk Bar 0.40, Swordsman 0.40),
  ambient particles with beams **0.06 ms** (0.01 without), emissive **0.09-0.13 ms** (0 when nothing emissive is drawn).
- 0 GL errors under `MESA_DEBUG=context` in every capture.
- **Off identity**: frozen Stock Pot Inn, `Interior.Windows=0` and `Emissive=0`: three captures of this build, plus the current-main
  tree's binary (`nd-taa`, same superproject tree and libultraship `83bc3ef3`) twice, share one PNG SHA-256 (`705ef49b...`). One
  earlier capture of this build landed on another frame (`bebf8dce...`, frame landing varies per run); features on is `767bdcf6...`.
  The fixture needs the cvars from `docs/WATER_FOAM.md` plus `Fog.Volumetric`, `SunShafts`, `Sky.Physical`, `Sky.Flare`, `Lighting.Local`,
  `Water.Enabled` and `TAA` off on both sides.
- 1:1 crops: pane edges, bars and pool penumbra are smooth; no banding seen.

## Limits
- Windows exist only in the listed rooms; the table has to be extended by hand (probe, then check on screen).
- Pools are screen-space: occluders that are off-screen do not shade them, and the surface must be visible.
- Beam direction ignores real sun azimuth; the same window gives the same pool at the same hour every day.
- Night-glass stencil and the emissive pass need the local-light stencil (`Lighting.Local` or TAA or `Interior.Emissive`).
- Windows have no depth (no reveal), and no glass reflection.
- Not tested in motion beyond static frames; no interior water or cutscene check.

## Developer aids
`gNewDawn.Interior.Debug=1` logs recorded openings, room bounds and first-seen texture draws (`INTDBG`).
`gNewDawn.Interior.Probe=1` logs world positions under a screen grid (`INTPROBE`). `gNewDawn.Interior.TestWindow="dx,dy,dz,nx,ny,nz,hw,hh"`
adds a window relative to the camera (prefix `a:` for world coordinates). Capture helper: `~/dev/ghm-mods/work/interior/` (private).
