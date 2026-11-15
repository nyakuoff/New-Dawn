# New Dawn VFX sprites (gNewDawn.Vfx.*)

Loaded by libultraship `gfx_opengl_vfx.cpp` from `<gNewDawn.Assets.Path>/vfx/`.

## Kenney sprites (CC0 1.0, unmodified)
Source: Kenney Particle Pack 1.1 by Kenney Vleugels (kenney.nl), https://kenney.nl/assets/particle-pack.
Mirror used: https://github.com/Calinou/kenney-particle-pack (rev ab7086639ee73be31abd87feb21bf1402d4e8144). Licence text: `LICENSE-kenney.txt`.
Files (512x512 PNG, byte-identical to the pack): flame_05 flame_06 fire_01 fire_02 smoke_01 smoke_04 smoke_07 spark_01
star_01 light_01 circle_05 magic_02.

## fire_flipbook_8x8.png (made for this project, CC0 1.0)
64-frame looping torch-fire flipbook, 8x8 grid of 160x256 cells (1280x2048 RGBA; R = heat, A = coverage; the game tints
it with the effect's own colours). Blender 5.2 Mantaflow fire/smoke bake, ray-marched along Y with numpy, last 8 frames
cross-faded into the first 8 for a seamless loop. Scripts: `tools/newdawn/vfx-flipbook/{bake_fire.py,render_sheet.py}`
(commands in its README). No third-party art.
