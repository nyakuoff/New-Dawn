# Night-sky pack (gNewDawn.Sky.Night)

Loaded by libultraship `gfx_opengl_postfx.cpp` from `<gNewDawn.Assets.Path>/nightsky/`. Both data files are derived,
baked by `tools/newdawn/nightsky/prep_nightsky.py` from public data.

| File | What | Source | Licence |
|---|---|---|---|
| `stars.bin` | 13,964 stars, V magnitude <= 6.9: J2000 unit vector, magnitude, tempered B-V colour (`NDST` + count + 7 floats each) | HYG Database v4.1 by David Nash, https://github.com/astronexus/HYG-Database (`hyg/CURRENT/hygdata_v41.csv`), itself compiled from the Hipparcos, Yale Bright Star and Gliese catalogues | **CC BY-SA 4.0** (`LICENSE-HYG.txt`). `stars.bin` is a derivative and is offered under the same licence. |
| `milkyway_eq.jpg` + `milkyway_eq.txt` | Milky Way panorama, 4096x2048, equatorial (RA/Dec). JPEG q95 4:4:4, pixel = 255 * (linear/scale)^(1/3); `milkyway_eq.txt` holds `gamma=3 scale=...` | NASA/Goddard Space Flight Center Scientific Visualization Studio, "Deep Star Maps 2020" (SVS 4851), https://svs.gsfc.nasa.gov/4851, `starmap_2020_8k_gal.exr`. Built from Hipparcos-2, Tycho-2 and Gaia DR2 | NASA media is not copyrighted; the SVS page asks for the credit below. The Gaia DR2 data is **CC BY-SA 3.0 IGO**, so this derivative is distributed under CC BY-SA 3.0 IGO (https://creativecommons.org/licenses/by-sa/3.0/igo/) |

## Credit lines
- Stars: HYG Database by David Nash (astronexus.com), CC BY-SA 4.0.
- Milky Way: NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC (CC BY-SA 3.0 IGO). Animator Ernie Wright.

## Changes made
- Stars: filtered to magnitude <= 6.9, B-V converted to a tempered linear colour, packed as binary.
- Panorama: bright catalogue stars despiked (stars are drawn separately), resampled from galactic 8192x4096 to an
  equatorial 4096x2048 map with bilinear filtering, cube-root encoded to 8 bits and JPEG-compressed (the raw half-float
  form was 50 MB; the file is 3.3 MB).

## Rebuild
```
magick starmap_2020_8k_gal.exr -depth 32 -define quantum:format=floating-point rgb:gal8k.f32
python3 -I tools/newdawn/nightsky/prep_nightsky.py --hyg hygdata_v41.csv --gal gal8k.f32 --out mm/assets/newdawn/nightsky
```
