#!/usr/bin/env python3
"""Self-rendered seamless water-caustics flipbook (CC0, no third-party art).

A periodic wave height field (sum of cosines with integer wave vectors, integer temporal frequencies) refracts a
bundle of parallel rays; the rays are splatted onto the floor with wrap-around, which yields the photon density, i.e.
the caustic. Tileable in space and in time. Output: 8 columns x 4 rows of 256x256 frames, 8-bit grey
(value = clamp(density / 4)), mean density 1 -> mean value 64.

  python3 gen_caustics.py /abs/out/caustics_8x4.png [seed]
"""
import sys
import numpy as np
from PIL import Image

N, FRAMES, COLS, ROWS = 256, 32, 8, 4
RAYS = 1024          # rays per axis per frame (16 per output texel)
DEPTH = 0.018        # floor depth * (1 - 1/eta), in tile units: how far slope pushes a ray
WAVES = 22

rng = np.random.default_rng(int(sys.argv[2]) if len(sys.argv) > 2 else 7)
kk = []
while len(kk) < WAVES:
    k = rng.integers(-8, 9, size=2)
    m = np.hypot(*k)
    if 3.0 <= m <= 8.5:
        kk.append(k)
kk = np.array(kk, dtype=np.float64)
amp = 1.0 / np.hypot(kk[:, 0], kk[:, 1]) ** 1.2           # slope ~ |k| * amp  -> mild fall-off, keeps small detail
amp *= 1.0 / np.sqrt((amp ** 2 * np.hypot(kk[:, 0], kk[:, 1]) ** 2).sum() / 2.0) / (2 * np.pi)  # unit rms slope
freq = rng.choice([-2, -1, 1, 2], size=WAVES)               # integer cycles per loop => seamless in time
phase = rng.uniform(0, 2 * np.pi, WAVES)

g = (np.arange(RAYS) + 0.5) / RAYS
X, Y = np.meshgrid(g, g)
frames = []
for f in range(FRAMES):
    t = f / FRAMES
    gx = np.zeros_like(X)
    gy = np.zeros_like(X)
    for (kx, ky), a, fr, ph in zip(kk, amp, freq, phase):
        arg = 2 * np.pi * (kx * X + ky * Y + fr * t) + ph
        s = -a * 2 * np.pi * np.sin(arg)                    # d/darg of a*cos(arg)
        gx += s * kx
        gy += s * ky
    u = (X - DEPTH * gx * 3.0) % 1.0
    v = (Y - DEPTH * gy * 3.0) % 1.0
    # bilinear splat with wrap-around
    px, py = u * N - 0.5, v * N - 0.5
    x0, y0 = np.floor(px).astype(int), np.floor(py).astype(int)
    fx, fy = px - x0, py - y0
    acc = np.zeros(N * N)
    for dx, dy, w in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        idx = ((y0 + dy) % N) * N + ((x0 + dx) % N)
        acc += np.bincount(idx.ravel(), weights=w.ravel(), minlength=N * N)
    acc = acc.reshape(N, N)
    frames.append(acc / acc.mean())
frames = np.array(frames)
print("density mean %.3f  p50 %.2f p99 %.2f max %.2f" % (frames.mean(), np.percentile(frames, 50),
                                                           np.percentile(frames, 99), frames.max()))
sheet = np.zeros((ROWS * N, COLS * N), np.uint8)
for f in range(FRAMES):
    r, c = divmod(f, COLS)
    sheet[r * N:(r + 1) * N, c * N:(c + 1) * N] = np.clip(frames[f] / 4.0 * 255.0 + 0.5, 0, 255).astype(np.uint8)
Image.fromarray(sheet, "L").save(sys.argv[1])
