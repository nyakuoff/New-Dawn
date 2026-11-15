# Grass tuft atlas (gNewDawn.Grass.*)

`tuft_atlas.png`: 2048x2048 RGBA, 2x2 cells of 1024x1024 grass tufts seen from the side (base on the bottom edge,
centred). RGB = neutral shading (dark root to bright tips, pale midrib), multiplied with the ground colour by the
renderer; A = blade coverage.
Made for this project, no third-party art: `tools/newdawn/grass-tufts/render_tufts.py OUT 7 1024` (numpy + Pillow,
seed 7). **CC0 1.0.** Loaded by libultraship `gfx_opengl_grass.cpp` from `<gNewDawn.Assets.Path>/grass/`.
