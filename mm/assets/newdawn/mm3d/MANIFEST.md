# 3DS character texture upscaler (gNewDawn.MM3D.Upscale)

`span_v4_4x.bin`: the weights of the 4x image upscaler "PBRify UpscalerSPANV4" (SPAN network, 22 convolution layers,
426,000 parameters), 1.7 MB of little-endian float32 after an 8-byte header ("SPAN", version 1). Layout and order are in
`tools/newdawn/mm3d/export_span.py`.

- Source: PBRify_Remix, https://github.com/Kim2091/PBRify_Remix, `4x-PBRify_UpscalerSPANV4.pth`. The repository is
  **CC0-1.0**; its README states the models were trained exclusively on CC0 material (ambientCG).
- Changes: converted to ONNX (torch export) and from there to this raw layout, no retraining and no value changes
  (verified against ONNX Runtime 1.32: max difference 1/255 on an upscaled texture).
- Used by `mm/2s2h/NewDawn/SpanUpscale.cpp`, which runs the network on the CPU. It upscales the player's 3DS character
  textures once, at conversion time, on their machine (`docs/MM3D_CHARACTERS.md`). Nothing from the game is in this file.
- Read at run time from `<gNewDawn.Assets.Path>/mm3d/`.
