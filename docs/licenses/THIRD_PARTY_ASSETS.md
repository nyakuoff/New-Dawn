# Third-party and bundled runtime assets

Everything under `mm/assets/newdawn/` ships with the game (decision ND-008). The build copies it to
`<build>/mm/assets/newdawn/` and `install` puts it in `assets/newdawn/`. Each pack also has a `MANIFEST.md` beside the files.
None of it comes from the game. Total size: about 66 MB (57 MB of it is the base material library).

| Pack / files | Used by | Author and source | Licence | Changes |
|---|---|---|---|---|
| `atmos/dust_64.png`, `atmos/splash_64.png` | dust motes, rain splashes | Kenney Vleugels (kenney.nl), Particle Pack 1.1, https://kenney.nl/assets/particle-pack (mirror: Calinou/kenney-particle-pack) | CC0 1.0 | `circle_05` / `circle_01` downsampled to 64x64 |
| `atmos/leaf_64.png` | falling leaves | cemkalyoncu, "Leaf Pack", https://opengameart.org/content/leaf-pack | CC0 1.0 | one SVG (`eastern redbud`) rasterised to 64 px |
| `vfx/*.png` (12 sprites) | fire, smoke, sparks, embers, magic | Kenney Vleugels, Particle Pack 1.1 (as above) | CC0 1.0 | none (byte-identical) |
| `vfx/fire_flipbook_8x8.png` | torch fire | made for New Dawn: `tools/newdawn/vfx-flipbook/` (Blender Mantaflow bake + numpy) | CC0 1.0 | n/a |
| `caustics/caustics_8x4.png` | water caustics | made for New Dawn: `tools/newdawn/caustics/gen_caustics.py` | CC0 1.0 | n/a |
| `water/flow_noise_256.png` | river flow streaks, waterfall streams | made for New Dawn: `tools/newdawn/water-flow/gen_flow_noise.py` | CC0 1.0 | n/a |
| `water/foam_lace_512.png` | shoreline and river foam (lace, clumps, bubbles) | made for New Dawn: `tools/newdawn/water-flow/gen_foam.py` | CC0 1.0 | n/a |
| `grass/tuft_atlas.png` | grass tufts | made for New Dawn: `tools/newdawn/grass-tufts/render_tufts.py` | CC0 1.0 | n/a |
| `grass/flower_atlas.png` | daisies, buttercups, violet wildflowers, clover patch | made for New Dawn: `tools/newdawn/grass-tufts/render_tufts.py OUT 7 1024 flower_atlas.png` | CC0 1.0 | n/a |
| `oga-lens-flares/flare/{star,extend,hexangle,iris,sun,ring}.png` | sun lens flare | hackcraft.de ("hc"), "Lens-Flares and Particles", https://opengameart.org/content/lens-flares-and-particles | CC-BY 3.0 or CC0 (page); archive says CC0. **Credited as CC-BY 3.0** | none (byte-identical) |
| `mm3d/span_v4_4x.bin` | 4x upscale of the player's own 3DS character textures (generated on their machine) | PBRify_Remix, model 4x-PBRify_UpscalerSPANV4, https://github.com/Kim2091/PBRify_Remix | CC0 1.0 (repo licence; trained on CC0 data per its README) | converted to a raw float32 layout, values unchanged |
| `materials/*.jpg`, `materials/manifest.json` | first-run remastered-texture generator (ND-016): input to `mm/2s2h/NewDawn/RuntimeTextures.cpp`, never shown unmodified | 35 sets from ambientCG (https://ambientcg.com), see `materials/MANIFEST.md` for every set with page, download URL and archive SHA-256 | CC0 1.0 (https://docs.ambientcg.com/license/) | re-encoded to 1024x1024 JPEG (albedo, normal GL, roughness, height) by `tools/newdawn/matlib/build_library.py` |
| `nightsky/stars.bin` | night stars | David Nash, HYG Database v4.1, https://github.com/astronexus/HYG-Database (from Hipparcos, Yale Bright Star, Gliese) | **CC BY-SA 4.0**; this derivative is under the same licence | filtered to magnitude <= 6.9, colour from B-V, binary packed |
| `nightsky/milkyway_eq.jpg`, `.txt` | Milky Way | NASA/GSFC Scientific Visualization Studio, "Deep Star Maps 2020" (SVS 4851), https://svs.gsfc.nasa.gov/4851; Gaia DR2: ESA/Gaia/DPAC; animator Ernie Wright | NASA media: not copyrighted, credit requested; Gaia DR2 **CC BY-SA 3.0 IGO**, so this derivative is distributed under CC BY-SA 3.0 IGO | stars despiked, galactic 8192x4096 resampled to equatorial 4096x2048, cube-root 8-bit JPEG (q95) |

## Texture library (ND-016): catalog only, files not yet committed
`tools/newdawn/texlib/` lists CC0 / MIT PBR materials (ambientCG, Poly Haven, TextureCan, AMD GPUOpen MaterialX) that the
texture pipeline may ship. The image files are **not** in the repository yet: the engine picks the final set and commits it
with a `MANIFEST.md`. Per-asset rows for the 100 shortlisted materials, with page, licence and SHA-256, are in
[`TEXLIB_CREDITS.md`](TEXLIB_CREDITS.md). Licence evidence per source is in `texlib/` (page snapshots, fetched 2026-11-16),
and the verdict for every source checked (including the excluded ones) is in `tools/newdawn/texlib/SOURCES.md`.
- ambientCG, Poly Haven, TextureCan: CC0 (credit given anyway).
- AMD GPUOpen MaterialX Library: **MIT**, so the copyright notice must ship with any material used (see `TEXLIB_CREDITS.md`).
- Excluded for not explicitly allowing redistribution: FreePBR, Poliigon, colormass, Adobe Substance Community Assets, PolyScan, ShareTextures, LotPixel, CGAxis, Texture.ninja, BlenderKit "Royalty Free" items.

## Required credit lines
- **Stars:** HYG Database by David Nash (astronexus.com), CC BY-SA 4.0.
- **Milky Way:** NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC (CC BY-SA 3.0 IGO).
- **Lens flares:** "Lens-Flares and Particles" by hackcraft.de via OpenGameArt, CC-BY 3.0 (https://creativecommons.org/licenses/by/3.0/).
- Kenney particle sprites, the leaf, New Dawn's own caustics, grass and fire art, and the PBRify SPAN upscaler weights are CC0 (credit not required, given anyway).

These appear in the menu under Settings > About ("Bundled art and data") and in the README.

## ShareAlike scope
CC BY-SA covers the two night-sky data files only (adaptations of the star catalogue and the panorama). It does not
extend to the renderer code, which stays CC0 1.0.

## Checksums of the unmodified lens-flare files (as downloaded)
```
da8c1c107833eb51c16a6d82331470628ad5f13bd08a4d51a679a8bfb71bfac8  star.png
77b17fd5a3ab908c3989c4ed5f139c8be7f2c02d176c74f9ee571b67c92c993e  extend.png
555e532026631eafc6a4cddf234b181179bcfb2ea1ea35cb2146a1842bc78495  hexangle.png
b288d6f750ea5cfc55b59b5e2bc1fb700362c80d56c67e97d6864ff54a9419b5  iris.png
bafab147b4e3fe5f3a715a753f516dda4cf2ac0309fd29e49be5ff86dec19eab  sun.png
1a662963ddcc5ee9c4131984a300b0a818ac0e86572e9b700a789ed8056388b2  ring.png
```

## Not included (derived from the game, generated on the player's machine)
The material maps (`zz-newdawn-materials.o2r`) and the texture recolour (`zy-newdawn-recolor.o2r`). Optional community
HD mods are separate downloads (see the menu's About page).
