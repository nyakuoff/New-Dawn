# Water-caustics flipbook (gNewDawn.Water.Caustics / .Underwater)

`caustics_8x4.png`: 2048x1024 8-bit grey, 32 frames in 8 columns x 4 rows of 256x256, seamless in space and time.
value = clamp(photon density / 4); mean density 1.
Made for this project, no third-party art: `tools/newdawn/caustics/gen_caustics.py` (seed 7): a periodic 22-wave height
field refracts a parallel ray bundle that is splatted onto the floor. **CC0 1.0.**
Loaded by libultraship `gfx_opengl_postfx.cpp` from `<gNewDawn.Assets.Path>/caustics/`.
