# VFX fire flipbook (offline authoring)

Generates `fire_flipbook_8x8.png`, the animated flame used by `gNewDawn.Vfx.*` (see `docs/VFX.md`). The output is an
authored asset: a Blender Mantaflow fire simulation rendered to a sprite sheet. Nothing here uses game art. CC0 like the repo.
Needs Blender (tested 5.2, headless) and Python with numpy + Pillow.

```sh
WIND=0.2 FUEL=1.8 BURN=0.35 BETA=2.8 TURB=2.0 NSTR=2.2 EMITR=0.06 NORENDER=1 \
  blender -b -P tools/newdawn/vfx-flipbook/bake_fire.py -- /abs/out/bake 80 1 1 120
python3 tools/newdawn/vfx-flipbook/render_sheet.py /abs/out/bake fire_flipbook_8x8.png 41 64 8 160 256
```
Copy the PNG into `assets/newdawn/vfx/` (the renderer reads it from `<gNewDawn.Assets.Path>/vfx/`). The renderer expects
exactly 1280x2048 (8 columns x 8 rows of 160x256).
