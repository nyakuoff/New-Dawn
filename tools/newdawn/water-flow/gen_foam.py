#!/usr/bin/env python3
"""Generates mm/assets/newdawn/water/foam_lace_512.png, the shoreline / river foam texture of the water surface pass.

RGBA8, 512x512, seamless in both axes (cellular noise wraps its lattice; the smooth noise is built in the Fourier domain).
  R  lace: thin bright filaments of a bubble network (Worley cell borders, broken up by noise), the "lacy" foam
  G  clumps: soft low-frequency patches (0..1) that decide where foam gathers
  B  bubbles: small soft discs of random size (spray / froth speckle)
  A  the finished foam density (lace gated by clumps, plus bubbles), what the shader mostly reads
Original work made for New Dawn, CC0 1.0. Deterministic (fixed seed). Usage: gen_foam.py OUT.png
"""
import sys
import numpy as np
from PIL import Image

N = 512


def worley(rng, cells, jitter=0.95):
    """Periodic Worley noise: returns (F1, F2) distances in units of one cell, on an N x N grid."""
    pts = (np.stack(np.meshgrid(np.arange(cells), np.arange(cells), indexing="ij"), -1) + 0.5 +
           (rng.random((cells, cells, 2)) - 0.5) * jitter)  # (cells, cells, (y, x))
    lin = (np.arange(N) + 0.5) / N * cells
    py, px = np.meshgrid(lin, lin, indexing="ij")
    f1 = np.full((N, N), 1e9)
    f2 = np.full((N, N), 1e9)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            # cell (cy+oy, cx+ox) feature point, wrapped
            cy = (np.floor(py).astype(int) + oy)
            cx = (np.floor(px).astype(int) + ox)
            fy = pts[cy % cells, cx % cells, 0] + (cy - cy % cells)
            fx = pts[cy % cells, cx % cells, 1] + (cx - cx % cells)
            d = np.hypot(py - fy, px - fx)
            lo = d < f1
            f2 = np.where(lo, f1, np.minimum(f2, d))
            f1 = np.where(lo, d, f1)
    return f1, f2


def band_noise(rng, lo, hi, falloff):
    f = np.fft.fftfreq(N) * N
    fx, fy = np.meshgrid(f, f)
    r = np.hypot(fx, fy)
    amp = np.where((r >= lo) & (r <= hi), np.maximum(r, 1.0) ** -falloff, 0.0) * np.exp(-((r / hi) ** 4))
    out = np.real(np.fft.ifft2(np.fft.fft2(rng.standard_normal((N, N))) * amp))
    return (out - out.mean()) / out.std()


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def main(path):
    rng = np.random.default_rng(20261115)
    # Lace: zero-crossing contours of band-limited noise are closed, wandering loops (bubble outlines); two scales, thin lines,
    # broken up by a third noise so the net has gaps.
    lace = np.zeros((N, N))
    for lo, hi, w, weight in ((6, 22, 0.26, 1.0), (14, 46, 0.3, 0.85), (30, 90, 0.34, 0.6)):
        n = band_noise(rng, lo, hi, 0.6)
        lace = np.maximum(lace, weight * (1.0 - smoothstep(0.0, w, np.abs(n))))
    lace *= smoothstep(-0.9, 0.4, band_noise(rng, 4, 30, 0.7))
    # Clumps: where foam gathers.
    clump = smoothstep(-0.7, 0.9, band_noise(rng, 2, 14, 1.1))
    # Bubbles: a field of small soft discs, random radii (Worley F1 with a per-cell size).
    bubbles = np.zeros((N, N))
    for cells, rad in ((46, 0.30), (80, 0.26)):
        f1, _ = worley(rng, cells, 1.0)
        size = rad * (0.4 + 0.6 * rng.random())
        bubbles = np.maximum(bubbles, 1.0 - smoothstep(size * 0.35, size, f1))
    bubbles *= smoothstep(-0.2, 0.6, band_noise(rng, 4, 40, 0.8))
    density = np.clip(np.maximum(lace * (0.35 + 0.65 * clump), bubbles * clump * 0.85), 0, 1)
    rgba = np.stack([lace, clump, bubbles, density], axis=-1)
    Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(path, optimize=True)
    print(f"wrote {path}; mean density {density.mean():.3f}, wrap-edge diff {np.abs(rgba[:, 0] - rgba[:, -1]).mean():.4f}, "
          f"interior diff {np.abs(rgba[:, 100] - rgba[:, 101]).mean():.4f}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "foam_lace_512.png")
