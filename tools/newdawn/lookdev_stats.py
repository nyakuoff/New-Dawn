#!/usr/bin/env python3
"""Tone and colour numbers of captures, for the look audit (docs/LOOK_BIBLE.md).

  lookdev_stats.py DIR_OR_PNG...          one row per image
  lookdev_stats.py --compare DIR_A DIR_B  per image present in both: are the files the same, and how far apart
  lookdev_stats.py --self-check

Columns, all on the display-encoded image with the HUD rows cropped (top 60, bottom 160 of 1080):
  mean p5 p50 p95  Rec.709 luma of the display-encoded pixel, 0..1
  range            stops between shade and light: log2 of the linear-light luma at p95 over p5
  blk% wht%        pixels below 4/255 and above 250/255
  sat              mean HSV saturation
  sh R/B, hi R/B   red/blue ratio of the darkest and brightest quarter: below 1 is cool, above 1 warm.
                   Their quotient (split) is the warm/cool separation between light and shade.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def stats(rgb):
    """rgb: float array (h, w, 3) in 0..1."""
    h = rgb.shape[0]
    rgb = rgb[int(h * 60 / 1080):h - int(h * 160 / 1080)].reshape(-1, 3)
    luma = rgb @ np.array([0.2126, 0.7152, 0.0722])
    p5, p25, p50, p75, p95 = np.percentile(luma, [5, 25, 50, 75, 95])
    mx, mn = rgb.max(1), rgb.min(1)
    sat = np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0).mean()
    rb = lambda sel: (rgb[sel, 0].mean() + 1e-4) / (rgb[sel, 2].mean() + 1e-4)
    sh, hi = rb(luma <= p25), rb(luma >= p75)
    lin = lambda v: max(((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92, 1e-4)  # sRGB decode
    return dict(mean=luma.mean(), p5=p5, p50=p50, p95=p95, range=np.log2(lin(p95) / lin(p5)),
                blk=100 * (mx < 4 / 255).mean(), wht=100 * (mn > 250 / 255).mean(), sat=sat, sh=sh, hi=hi, split=hi / sh)


def difference(a, b):
    """a, b: uint8 arrays of one shape. RMSE on a 0..1 scale, largest channel difference in code values, differing pixels."""
    d = a.astype(np.int16) - b.astype(np.int16)
    return float(np.sqrt((d.astype(np.float64) ** 2).mean()) / 255), int(np.abs(d).max()), int((d != 0).any(-1).sum())


def compare(dir_a, dir_b):
    """Fixture noise moves pixels about (rmse) without moving the tone (d-median, d-sat); a look change moves both."""
    names = sorted({p.name for p in Path(dir_a).glob("*.png")} & {p.name for p in Path(dir_b).glob("*.png")})
    print(f"{'image':44}{'same file':>10}{'rmse':>10}{'max':>5}{'pixels':>9}{'d-median':>10}{'d-sat':>8}")
    same = 0
    for n in names:
        fa, fb = Path(dir_a) / n, Path(dir_b) / n
        a, b = (np.asarray(Image.open(f).convert("RGB")) for f in (fa, fb))
        rmse, peak, count = difference(a, b)
        sa, sb = stats(a / 255.0), stats(b / 255.0)
        identical = fa.read_bytes() == fb.read_bytes()
        same += identical
        print(f"{n[:-4]:44}{'yes' if identical else 'no':>10}{rmse:10.6f}{peak:5d}{count:9d}"
              f"{sb['p50'] - sa['p50']:10.4f}{sb['sat'] - sa['sat']:8.4f}")
    print(f"{same} of {len(names)} files identical")


def self_check():
    grey = np.full((1080, 8, 3), 0.5)
    s = stats(grey)
    assert abs(s["mean"] - 0.5) < 1e-6 and s["range"] == 0 and s["sat"] == 0 and abs(s["split"] - 1) < 1e-6
    warm_light = np.zeros((1080, 8, 3))
    warm_light[:, :4] = (0.1, 0.1, 0.2)  # cool shade
    warm_light[:, 4:] = (0.8, 0.6, 0.4)  # warm light
    s = stats(warm_light)
    a = np.zeros((4, 4, 3), np.uint8)
    b = a.copy()
    b[0, 0] = (0, 255, 0)
    assert difference(a, a) == (0.0, 0, 0) and difference(a, b)[1:] == (255, 1) and 0.14 < difference(a, b)[0] < 0.15
    assert s["sh"] < 1 < s["hi"] and s["split"] > 3 and 4.5 < s["range"] < 5.5, s
    print("ok")


def main():
    if sys.argv[1:] == ["--self-check"]:
        return self_check()
    if len(sys.argv) == 4 and sys.argv[1] == "--compare":
        return compare(sys.argv[2], sys.argv[3])
    files = []
    for a in sys.argv[1:]:
        p = Path(a)
        files += sorted(p.glob("*.png")) if p.is_dir() else [p]
    cols = ["mean", "p5", "p50", "p95", "range", "blk", "wht", "sat", "sh", "hi", "split"]
    print(f"{'image':44}" + "".join(f"{c:>7}" for c in cols))
    for f in files:
        s = stats(np.asarray(Image.open(f).convert("RGB"), dtype=np.float64) / 255)
        print(f"{f.stem:44}" + "".join(f"{s[c]:7.2f}" if c in ("range", "blk", "wht", "split") else f"{s[c]:7.3f}" for c in cols))


if __name__ == "__main__":
    main()
