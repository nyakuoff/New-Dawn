#!/usr/bin/env python3
"""Self-rendered grass tuft cards for gNewDawn.Grass.* (no third-party art).

Writes a 2x2 atlas (default 1024x1024 RGBA, 512x512 cells). Each cell is one tuft seen from the side, base centred on the bottom
edge. RGB is a neutral shading value (dark ambient-occluded base -> bright tips, pale midrib, small per-blade tint drift)
that the shader multiplies with the ground colour; A is coverage.   python3 render_tufts.py OUT.png [seed] [cell] [FLOWERS.png]
(cell = 1024 writes the 2048x2048 atlas the game ships since #83: crisper blades at 1:1)

With a fourth argument it also writes the ground-cover atlas (gNewDawn.Foliage.Flowers): 2x2 cells of 512x512 side views,
daisies, buttercups, violet wildflowers and a clover patch with white heads. Unlike the tufts these carry their real colour
(the shader only lights them). Own seed, so the tuft atlas stays bit-identical to the shipped one.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

CELL = int(sys.argv[3]) if len(sys.argv) > 3 else 512
SS = 4 if CELL <= 512 else 3  # cell size and supersampling
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
    for _ in range(14 * CELL // 512):
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


# ---------------------------------------------------------------- ground cover (flowers, clover)
FS = 4  # supersampling for the flower cells
FC = 512
frng = np.random.default_rng(31)


def _col(c, lum=1.0):
    return tuple(int(255 * min(1.0, max(0.0, v * lum))) for v in c) + (255,)


def _stem(d, S, x0, y0, x1, y1, w, lo=0.45, hi=0.85):
    """Slightly curved stem, darker at the root."""
    n = 14
    for i in range(n):
        t0, t1 = i / n, (i + 1) / n
        mid = lambda t: (x0 + (x1 - x0) * t + np.sin(t * 3.14159) * 0.02 * S * np.sign(x1 - x0 + 1e-6), y0 + (y1 - y0) * t)
        c = (0.16 * (lo + (hi - lo) * t0) * 2.2, 0.42 * (lo + (hi - lo) * t0) * 1.6, 0.10 * (lo + (hi - lo) * t0) * 1.6)
        d.line([mid(t0), mid(t1)], fill=_col(c), width=int(w))


def _leaf(d, S, x, y, ang, ln, w, lum=1.0):
    pts = []
    for t in np.linspace(0, 1, 12):
        pts.append((x + np.cos(ang) * ln * t + np.sin(ang) * w * np.sin(t * 3.14159) * 0.5,
                    y - np.sin(ang) * ln * t + np.cos(ang) * w * np.sin(t * 3.14159) * 0.5))
    for t in np.linspace(1, 0, 12):
        pts.append((x + np.cos(ang) * ln * t - np.sin(ang) * w * np.sin(t * 3.14159) * 0.5,
                    y - np.sin(ang) * ln * t - np.cos(ang) * w * np.sin(t * 3.14159) * 0.5))
    d.polygon(pts, fill=_col((0.20 * lum, 0.52 * lum, 0.13 * lum)))


def _head(d, cx, cy, r, petals, petal_col, centre_col, flat=0.62, npet=10, spread=1.0):
    """A flower seen from slightly above: petals as ellipses on a flattened ring, then the centre disc."""
    for k in range(npet):
        a = 2 * np.pi * (k + frng.uniform(-0.2, 0.2)) / npet
        px, py = cx + np.cos(a) * r * 0.55 * spread, cy + np.sin(a) * r * 0.55 * flat * spread
        lum = 0.82 + 0.18 * (np.sin(a) * -0.5 + 0.5)       # far petals a touch darker than near ones
        er = r * petals
        pts = []
        for t in np.linspace(0, 2 * np.pi, 16, endpoint=False):
            ex, ey = np.cos(t) * er * 0.62, np.sin(t) * er * 0.30
            pts.append((px + ex * np.cos(a) - ey * np.sin(a), py + (ex * np.sin(a) + ey * np.cos(a)) * flat * 1.4))
        d.polygon(pts, fill=_col(petal_col, lum))
    d.ellipse([cx - r * 0.26, cy - r * 0.26 * flat * 1.2, cx + r * 0.26, cy + r * 0.26 * flat * 1.2], fill=_col(centre_col))


def _flower_cell(kind):
    S = FC * FS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    base = S - 0.01 * S
    if kind == 3:                                        # clover patch: low three-leaflet leaves, a few white pom-pom heads
        for _ in range(70):
            x = S / 2 + np.clip(frng.normal(0, 0.12 * S), -0.22 * S, 0.22 * S)
            y = base - frng.uniform(0.0, 0.07) * S
            h = frng.uniform(0.07, 0.17) * S
            tx = x + frng.normal(0, 0.02 * S)
            _stem(d, S, x, y, tx, y - h, 0.006 * S, 0.5, 0.8)
            lr = frng.uniform(0.032, 0.05) * S
            ly = y - h
            lum = frng.uniform(0.78, 1.05)
            for dx, dy in ((-0.9, 0.15), (0.9, 0.15), (0.0, -0.55)):
                ex, ey = tx + dx * lr, ly + dy * lr * 0.7
                d.ellipse([ex - lr * 0.85, ey - lr * 0.62, ex + lr * 0.85, ey + lr * 0.62], fill=_col((0.14 * lum, 0.46 * lum, 0.10 * lum)))
                d.arc([ex - lr * 0.55, ey - lr * 0.4, ex + lr * 0.55, ey + lr * 0.4], 200, 340, fill=_col((0.55 * lum, 0.78 * lum, 0.45 * lum)), width=int(0.004 * S))
        for _ in range(5):
            x = S / 2 + frng.normal(0, 0.14 * S)
            h = frng.uniform(0.20, 0.34) * S
            _stem(d, S, x, base, x + frng.normal(0, 0.02 * S), base - h, 0.007 * S)
            r = frng.uniform(0.036, 0.05) * S
            for _ in range(16):
                a, rr = frng.uniform(0, 6.283), frng.uniform(0.2, 1.0) * r
                px, py = x + np.cos(a) * rr, base - h + np.sin(a) * rr * 0.9
                d.ellipse([px - r * 0.32, py - r * 0.32, px + r * 0.32, py + r * 0.32], fill=_col((0.96, 0.94, 0.90), frng.uniform(0.84, 1.0)))
    else:
        specs = {0: dict(n=7, hmin=0.30, hmax=0.62, r=0.058, petals=0.95, col=(0.98, 0.98, 0.96), cen=(0.98, 0.76, 0.12), np_=12),
                 1: dict(n=9, hmin=0.26, hmax=0.55, r=0.040, petals=1.10, col=(1.0, 0.80, 0.08), cen=(0.95, 0.62, 0.05), np_=6),
                 2: dict(n=6, hmin=0.34, hmax=0.70, r=0.034, petals=1.0, col=(0.62, 0.36, 0.86), cen=(0.95, 0.88, 0.50), np_=5)}[kind]
        for _ in range(specs['n'] // 2 + 2):             # a rosette of leaves behind the stems
            x = S / 2 + frng.normal(0, 0.12 * S)
            side = frng.choice([-1, 1])
            _leaf(d, S, x, base, (np.pi / 2) - side * frng.uniform(0.35, 1.05), frng.uniform(0.12, 0.22) * S, 0.035 * S, frng.uniform(0.8, 1.05))
        stems = sorted([(frng.uniform(specs['hmin'], specs['hmax']), S / 2 + frng.normal(0, 0.13 * S)) for _ in range(specs['n'])])
        for h, x in stems:
            hx = x + frng.normal(0, 0.03 * S)
            _stem(d, S, x, base, hx, base - h * S, 0.007 * S)
            r = specs['r'] * S * frng.uniform(0.85, 1.15)
            if kind == 2:                                # spike of small florets up the top of the stem
                for j in range(9):
                    t = j / 8.0
                    _head(d, hx + frng.normal(0, 0.006 * S), base - h * S + t * 0.10 * S, r * (1.0 - 0.55 * t), 0.9, specs['col'], specs['cen'], npet=specs['np_'])
            else:
                _head(d, hx, base - h * S, r, specs['petals'], specs['col'], specs['cen'], npet=specs['np_'])
    a = np.asarray(img).astype(np.float32) / 255.0
    pre = a[..., :3] * a[..., 3:4]
    pre = np.asarray(Image.fromarray((np.dstack([pre, a[..., 3:4]]) * 255).astype(np.uint8)).resize((FC, FC), Image.LANCZOS)).astype(np.float32) / 255
    alpha = pre[..., 3:4]
    rgb = np.where(alpha > 1e-3, pre[..., :3] / np.maximum(alpha, 1e-3), 0.0)
    valid = alpha[..., 0] > 0.02
    acc, cnt = np.zeros_like(rgb), valid.astype(np.float32)
    acc[valid] = rgb[valid]
    for _ in range(14):                                  # no black fringes in the mips
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


if len(sys.argv) > 4:
    fl = np.zeros((2 * FC, 2 * FC, 4), np.float32)
    for i in range(4):
        fl[(i // 2) * FC:(i // 2 + 1) * FC, (i % 2) * FC:(i % 2 + 1) * FC] = _flower_cell(i)
    Image.fromarray((fl * 255 + 0.5).astype(np.uint8), "RGBA").save(sys.argv[4])
    print("wrote", sys.argv[4])
