#!/usr/bin/env python3
"""Bake the New Dawn night-sky asset pack (stars.bin + milkyway_eq.f16) from public data.

Inputs (downloaded by the developer, never committed):
  --hyg   hygdata_v41.csv                      HYG star database v4.1 (CC BY-SA 4.0, astronexus/HYG-Database)
  --gal   gal8k.f32                            NASA SVS 4851 "Deep Star Maps 2020" starmap_2020_8k_gal.exr as raw float32 RGB
                                               (8192x4096, galactic coordinates; public domain, NASA/Goddard SVS):
                                               magick starmap_2020_8k_gal.exr -depth 32 -define quantum:format=floating-point rgb:gal8k.f32
Outputs in --out:
  stars.bin       'NDST' u32 count, then count x 7 f32: x y z (J2000 equatorial unit vector), V magnitude, r g b
                  (linear, luminance 1, tempered B-V colour)
  milkyway_eq.f16 'NDMW' u32 width u32 height f32 scale, then width*height*3 IEEE half (RGB, linear, value*scale = radiance
                  relative to a unit with Vega-peak 1); equirectangular in RA (u, 0..24h increasing right) / Dec (v, +90 at top).
"""
import argparse, struct, csv, math
import numpy as np

# Equatorial J2000 -> galactic rotation (rows = galactic x,y,z).
EQ2GAL = np.array([[-0.0548755604, -0.8734370902, -0.4838350155],
                   [0.4941094279, -0.4448296300, 0.7469822445],
                   [-0.8676661490, -0.1980763734, 0.4559837762]])

def bv_to_rgb(bv):
    bv = min(max(bv, -0.40), 2.00)
    T = 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62))
    T = min(max(T, 2500.0), 25000.0)
    if T < 4000.0:
        x = -0.2661239e9 / T**3 - 0.2343589e6 / T**2 + 0.8776956e3 / T + 0.179910
    else:
        x = -3.0258469e9 / T**3 + 2.1070379e6 / T**2 + 0.2226347e3 / T + 0.240390
    if T < 2222.0:
        y = -1.1063814 * x**3 - 1.34811020 * x**2 + 2.18555832 * x - 0.20219683
    elif T < 4000.0:
        y = -0.9549476 * x**3 - 1.37418593 * x**2 + 2.09137015 * x - 0.16748867
    else:
        y = 3.0817580 * x**3 - 5.87338670 * x**2 + 3.75112997 * x - 0.37001483
    X, Y, Z = x / y, 1.0, (1.0 - x - y) / y
    r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z
    g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z
    b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z
    c = np.maximum(np.array([r, g, b]), 0.0)
    c = 0.45 + 0.55 * c / max(c.max(), 1e-6)  # temper: keep a slight colour-temperature variety, not rainbow dots
    c = c / (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2])
    return c

def stars(hyg, out, maxmag):
    rows = []
    with open(hyg, newline='') as f:
        for r in csv.DictReader(f):
            if r['id'] == '0':
                continue
            m = float(r['mag'])
            if m > maxmag:
                continue
            ra = float(r['ra']) * math.pi / 12.0
            dec = float(r['dec']) * math.pi / 180.0
            bv = float(r['ci']) if r['ci'] not in ('', None) else 0.6
            c = bv_to_rgb(bv)
            rows.append((math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec), m, *c))
    arr = np.array(rows, dtype='<f4')
    with open(out, 'wb') as f:
        f.write(b'NDST' + struct.pack('<I', len(arr)))
        f.write(arr.tobytes())
    print('stars', len(arr), 'brightest', arr[:, 3].min())

def boxmean(a, r):
    k = 2 * r + 1
    p = np.pad(a, ((r, r), (r, r), (0, 0)), mode='edge')
    c = np.cumsum(np.cumsum(p, 0), 1)
    c = np.pad(c, ((1, 0), (1, 0), (0, 0)))
    h, w = a.shape[:2]
    return (c[k:k + h, k:k + w] - c[:h, k:k + w] - c[k:k + h, :w] + c[:h, :w]) / (k * k)

def minfilter(a, r):
    out = a.copy()
    h, w = a.shape[:2]
    p = np.pad(a, ((r, r), (r, r), (0, 0)), mode='edge')
    for dy in range(2 * r + 1):
        for dx in range(2 * r + 1):
            np.minimum(out, p[dy:dy + h, dx:dx + w], out=out)
    return out

def milkyway(gal, out, W, H, spike):
    g = np.fromfile(gal, dtype='<f4').reshape(4096, 8192, 3)
    # Despike: catalogue stars are drawn separately and sharp; keep the map's unresolved glow, dust lanes and faint grain.
    bg = boxmean(minfilter(g, 2), 3)
    g = np.minimum(g, bg * spike + 0.004)
    # Equatorial pixel centres -> direction -> galactic lon/lat -> bilinear sample (galactic map: centre = l 0, l grows left).
    ra = (np.arange(W) + 0.5) / W * 2 * math.pi
    dec = (0.5 - (np.arange(H) + 0.5) / H) * math.pi
    RA, DEC = np.meshgrid(ra, dec)
    v = np.stack([np.cos(DEC) * np.cos(RA), np.cos(DEC) * np.sin(RA), np.sin(DEC)], -1)
    gv = v @ EQ2GAL.T
    l = np.arctan2(gv[..., 1], gv[..., 0])
    b = np.arcsin(np.clip(gv[..., 2], -1, 1))
    u = (0.5 - l / (2 * math.pi)) * 8192 - 0.5
    w_ = (0.5 - b / math.pi) * 4096 - 0.5
    u0 = np.floor(u).astype(int); v0 = np.floor(w_).astype(int)
    fu = (u - u0)[..., None]; fv = (w_ - v0)[..., None]
    def tap(uu, vv):
        return g[np.clip(vv, 0, 4095), uu % 8192]
    res = (tap(u0, v0) * (1 - fu) * (1 - fv) + tap(u0 + 1, v0) * fu * (1 - fv) +
           tap(u0, v0 + 1) * (1 - fu) * fv + tap(u0 + 1, v0 + 1) * fu * fv)
    scale = float(res.max())
    with open(out, 'wb') as f:
        f.write(b'NDMW' + struct.pack('<IIf', W, H, scale))
        f.write((res / scale).astype('<f2').tobytes())
    print('milkyway', W, H, 'scale', scale, 'mean', res.mean(), 'p99', np.percentile(res, 99))
    return res

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--hyg', required=True); ap.add_argument('--gal', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--maxmag', type=float, default=6.9)
    ap.add_argument('--width', type=int, default=4096)
    ap.add_argument('--spike', type=float, default=3.0)
    ap.add_argument('--preview')
    a = ap.parse_args()
    stars(a.hyg, a.out + '/stars.bin', a.maxmag)
    res = milkyway(a.gal, a.out + '/milkyway_eq.f16', a.width, a.width // 2, a.spike)
    if a.preview:
        from PIL import Image
        img = np.clip(res / np.percentile(res, 99.5), 0, 1) ** (1 / 2.2)
        Image.fromarray((img * 255).astype(np.uint8)).resize((2048, 1024)).save(a.preview)
