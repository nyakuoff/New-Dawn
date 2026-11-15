#!/usr/bin/env python3
"""Generates mm/assets/newdawn/water/flow_noise_256.png, the tileable noise the river and waterfall passes advect.

RGBA8, 256x256, seamless in both axes (everything is built in the Fourier domain, so it is periodic by construction):
  R, G  slope (d height / dx, d height / dy) of a band-limited height field, normalised to +-1 and encoded 0..255
  B     the height field itself
  A     ridged streak mask (1 on thin bright ridges), the foam/streak pattern
Original work made for New Dawn, CC0 1.0. Deterministic (fixed seed). Usage: gen_flow_noise.py OUT.png
"""
import sys
import numpy as np
from PIL import Image

N = 256


def band_noise(rng, lo, hi, falloff):
    """Periodic noise whose spectrum is a ring (lo..hi cycles per tile) with a 1/f^falloff slope."""
    f = np.fft.fftfreq(N) * N
    fx, fy = np.meshgrid(f, f)
    r = np.hypot(fx, fy)
    amp = np.where((r >= lo) & (r <= hi), (np.maximum(r, 1.0)) ** -falloff, 0.0)
    amp *= np.exp(-((r / hi) ** 4))  # soft top edge
    white = np.fft.fft2(rng.standard_normal((N, N)))
    out = np.real(np.fft.ifft2(white * amp))
    return (out - out.mean()) / out.std()


def main(path):
    rng = np.random.default_rng(20261115)
    height = band_noise(rng, 2, 40, 1.15)
    # periodic central differences (np.roll wraps), then normalise the slope field to +-1
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    scale = np.percentile(np.hypot(dx, dy), 99)
    dx = np.clip(dx / scale, -1, 1)
    dy = np.clip(dy / scale, -1, 1)
    h = (height - height.min()) / (height.max() - height.min())
    ridge = 1.0 - np.abs(band_noise(rng, 3, 56, 0.9)) * 0.9
    ridge = np.clip((ridge - 0.45) / 0.5, 0, 1) ** 2.2
    rgba = np.stack([dx * 0.5 + 0.5, dy * 0.5 + 0.5, h, ridge], axis=-1)
    Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA").save(path, optimize=True)
    # seam check: wrapping neighbours must be as similar as interior neighbours
    seam = np.abs(rgba[:, 0] - rgba[:, -1]).mean()
    inner = np.abs(rgba[:, 100] - rgba[:, 101]).mean()
    print(f"wrote {path}; wrap-edge diff {seam:.4f}, interior diff {inner:.4f}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "flow_noise_256.png")
