#!/usr/bin/env python3
"""Self-rendered grass tuft cards for gNewDawn.Grass.* (no third-party art).

Writes a 2x2 atlas (1024x1024 RGBA, 512x512 cells). Each cell is one tuft seen from the side, base centred on the bottom
edge. RGB is a neutral shading value (dark ambient-occluded base -> bright tips, pale midrib, small per-blade tint drift)
that the shader multiplies with the ground colour; A is coverage.   python3 render_tufts.py OUT.png [seed]
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

CELL, SS = 512, 4          # cell size and supersampling
rng = np.random.default_rng(int(sys.argv[2]) if len(sys.argv) > 2 else 7)


def bez(p0, p1, p2, t):
    return (1 - t) ** 2 * p0 + 2 * (1 - t) * t * p1 + t * t * p2


def draw_blade(d, base, tip, bend, width, tint, rib):
    """Tapered quadratic blade from base to tip; drawn as 28 quads so the colour can run along its length."""
    base, tip = np.array(base, float), np.array(tip, float)
    mid = (base + tip) / 2 + np.array(bend, float)
    n = 28
    ts = np.linspace(0, 1, n + 1)
    pts = np.array([bez(base, mid, tip, t) for t in ts])
    tan = np.gradient(pts, axis=0)
    tan /= np.linalg.norm(tan, axis=1, keepdims=True) + 1e-9
    nrm = np.stack([-tan[:, 1], tan[:, 0]], 1)
    hw = width * (1 - ts) ** 0.75 * 0.5 + 0.6 * SS
    left, right = pts + nrm * hw[:, None], pts - nrm * hw[:, None]
    for i in range(n):
        t = (ts[i] + ts[i + 1]) / 2
        lum = 0.30 + 0.70 * t ** 0.8                   # baked contact shadow at the root, bright tips
        c = np.clip(np.array([lum * tint[0], lum * tint[1], lum * tint[2]]), 0, 1)
        d.polygon([tuple(left[i]), tuple(left[i + 1]), tuple(right[i + 1]), tuple(right[i])],
                  fill=tuple(int(255 * v) for v in c) + (255,))
        if rib:                                         # pale midrib on the wider lower half of the blade
            m0, m1 = pts[i], pts[i + 1]
            w = max(hw[i] * 0.22, 0.5 * SS)
            cc = np.clip(c * 1.18 + 0.04, 0, 1)
            d.line([tuple(m0), tuple(m1)], fill=tuple(int(255 * v) for v in cc) + (255,), width=int(w * 2))


def tuft(blades, spread, hmin, hmax, wmin, wmax, curl):
    S = CELL * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    items = []
    for _ in range(blades):
        ang = rng.normal(0, spread)                     # lean from vertical, radians
        h = rng.uniform(hmin, hmax) * S
        x0 = S / 2 + rng.normal(0, 0.04 * S)
        y0 = S - 0.01 * S
        tip = (x0 + np.sin(ang) * h * 0.62 + rng.normal(0, 0.02 * S), y0 - np.cos(ang) * h)
        bend = (np.sign(ang if ang else 1) * rng.uniform(0.2, 1.0) * curl * h * 0.7, rng.uniform(-0.05, 0.12) * h)
        w = rng.uniform(wmin, wmax) * S
        tint = np.array([1.0, 1.0, 1.0]) * rng.uniform(0.88, 1.0)
        tint[0] *= rng.uniform(0.97, 1.03)              # a hint of warm / cool drift between blades
        tint[2] *= rng.uniform(0.96, 1.0)
        items.append((h, (x0, y0), tip, bend, w, tint))
    items.sort(key=lambda it: it[0])                    # short blades behind, tall in front
    for h, b, t, bd, w, tint in items:
        draw_blade(d, b, t, bd, w, tint, rib=w > 0.03 * S)
    a = np.asarray(img).astype(np.float32) / 255.0
    # alpha-weighted downsample so edges carry blade colour instead of black fringes
    pre = a[..., :3] * a[..., 3:4]
    pre = np.asarray(Image.fromarray((np.dstack([pre, a[..., 3:4]]) * 255).astype(np.uint8)).resize((CELL, CELL), Image.LANCZOS)).astype(np.float32) / 255
    alpha = pre[..., 3:4]
    rgb = np.where(alpha > 1e-3, pre[..., :3] / np.maximum(alpha, 1e-3), 0.0)
    # fill transparent texels with nearby blade colour (so mipmaps/bilinear never pull in black)
    fill = rgb.copy()
    valid = (alpha[..., 0] > 0.02)
    acc, cnt = np.zeros_like(rgb), valid.astype(np.float32)
    acc[valid] = rgb[valid]
    for _ in range(14):
        k = np.zeros_like(cnt)
        a2 = np.zeros_like(acc)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k += np.roll(cnt, (dy, dx), (0, 1))
            a2 += np.roll(acc, (dy, dx), (0, 1))
        new = (cnt == 0) & (k > 0)
        acc[new] = a2[new] / k[new][:, None]
        cnt = np.where(new, 1.0, cnt)
    rgb = np.where(valid[..., None], rgb, acc)
    return np.dstack([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)])


cells = [tuft(64, 0.34, 0.50, 0.96, 0.030, 0.052, 0.18),
         tuft(80, 0.44, 0.42, 0.90, 0.026, 0.046, 0.24),
         tuft(48, 0.24, 0.60, 0.99, 0.034, 0.058, 0.12),
         tuft(90, 0.50, 0.38, 0.84, 0.024, 0.044, 0.28)]
atlas = np.zeros((2 * CELL, 2 * CELL, 4), np.float32)
for i, c in enumerate(cells):
    atlas[(i // 2) * CELL:(i // 2 + 1) * CELL, (i % 2) * CELL:(i % 2 + 1) * CELL] = c
Image.fromarray((atlas * 255 + 0.5).astype(np.uint8), "RGBA").save(sys.argv[1])
print("wrote", sys.argv[1])
