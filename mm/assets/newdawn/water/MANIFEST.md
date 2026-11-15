# River and waterfall flow noise (gNewDawn.Water.Rivers / .Waterfalls)

`flow_noise_256.png`: 256x256 RGBA8, seamless in both axes. R,G = slope of a band-limited height field (+-1 encoded 0..255),
B = the height, A = ridged streak mask. The river surface pass advects it along the native texture-scroll direction
(two-phase flow map) and the waterfall pass streams it down the fall.
Made for this project, no third-party art: `tools/newdawn/water-flow/gen_flow_noise.py` (fixed seed, Fourier-domain
synthesis, so it tiles by construction). **CC0 1.0.**
Loaded by libultraship `gfx_opengl_postfx.cpp` from `<gNewDawn.Assets.Path>/water/`.
The spray, mist and foam sprites are the CC0 Kenney sprites in `../atmos/` (see its MANIFEST).
