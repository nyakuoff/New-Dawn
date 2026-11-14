# Character lighting

`gNewDawn.Lighting.Characters` (default 0; on in the New Dawn preset) and `gNewDawn.Lighting.CharactersStrength`
(0..2, default 1). Requires HDR and per-pixel lighting. Menu: Lighting, "Character Lighting".

## What it does
Lit actor draws get two additive terms in `newDawnCharacter` (default.shader.glsl, applied after the HDR diffuse):
- **Rim**: Fresnel term on the world normal, tinted by the sky irradiance cube sampled along the reflected view
  direction (unit 12, as #43), or by the scene ambient where sky ambient is not active (interiors). Directional and
  point lights add to it only when they are behind the subject as seen from the camera. The rim multiplies the
  albedo, so dark texels do not glow.
- **Specular**: GGX from the same light set. Material class comes from the albedo only (bright neutral = metal,
  skin hues = none, cloth = faint sheen). The lobe is bounded per channel by the lit albedo, so it cannot lift dark
  texels. The effect fades out between 400 and 800 world units from the camera.

## Classification (render state only)
A draw is a character when HDR and per-pixel lighting are on, the modelview matrix is not the identity (room
meshes use it), and at least two modelview matrices were loaded since the last `G_MW_NUMLIGHT` (`Actor_Draw` binds
lights once, then a skeleton loads one matrix per limb). Props drawn as actors (stalls, Clock Tower, doors) load a
single matrix and stay on the original shading. The camera position is solved from P (view*projection); it has no
solution for the HUD's orthographic projection, which therefore stays off. The root limb of a skeleton loads only
one matrix and is not lit by this effect.

## Limitations
- Sun shadows are applied afterwards in screen space to the whole colour, so a shadowed actor's specular is dimmed
  by the shadow floor (0.65 in the preset), not removed.
- Materials: companion-map surfaces (`gNewDawn.Materials`) keep their own specular.
- OpenGL only; GLES keeps the existing path.

## Evidence (2026-11-13, RX 7800 XT, preset + full mods)
Same-frame splits (left half off, right half on, from a temporary debug split that is not committed) and 2x crops:
`~/dev/ghm-mods/showcase/characters/split/{noon,dusk}-{south_clock_town,termina_field,southern_swamp,stock_pot_inn}`.
Off (HDR on) and Characters=1 with HDR off are pixel-identical to the previous main on a frozen Stock Pot Inn
frame. 0 GL errors with `MESA_DEBUG=context`. World draw batches: Termina Field 0.458 -> 0.467 ms, South Clock
Town 0.594 -> 0.606 ms (about +0.01 ms).

## Soft shading and shiny metal (fix #82, 2026-11-14)
Low-poly actor meshes exposed hard facet edges in the rim and highlights, and metal items looked pale.
- **Soft normal.** Everything that is not metal (rim, wrap, cloth/skin sheen) reads from the world normal pulled halfway toward the
  view vector, with a wider, lower-gain rim (`pow(.., 1.5)`, gain 3.4 instead of `pow(.., 2)` x 5.5), a wrapped back-light term and
  broader lobes (roughness 0.72). Point-light specular on non-metal is halved. The hat and NPC arms lose their facet-edge glints.
- **Metal.** Bright neutral texels (luma 0.22..0.5, fading out above 0.7 so white cloth, eyes and socks stay matte) are metal:
  roughness 0.24 core plus a broad lobe, F0 tinted by the albedo's own hue (`0.92 * (albedo / max)^3`), a budget of 4.5x albedo
  instead of 0.93x, 50% less diffuse, and a reflection of the prefiltered sky cube (`uSkySpecular`, unit 13, the same cube the
  material maps use; its declaration moved into the per-pixel block, the unit was already bound to a dummy at program creation)
  lifted x10 because the cube holds radiance well below sunlit surfaces. Shield rim: +22..35% brighter with a visible gradient
  instead of a flat grey (Clock Town day/dusk, `showcase/fix-82/crop-sh16.png`, `crop-sct_day.png`).
- Off (`Characters=0`) is unchanged: the frozen Stock Pot Inn (clock 12, frame 30, the rules' off cvar list) has the same
  SHA-256 `6286c8a1...` with the old and the new shader (three runs; the first run of a session landed on another frame, as documented).
