#!/usr/bin/env python3
"""GPU cost of the look tiers on the eight scenes of docs/PERFORMANCE.md: one live game run per cell (real game logic,
60 fps interpolation, vsync off, MESA_DEBUG=context), through nd-heavy.

  lookdev_perf.py OUT_DIR [--tiers ultra,high,medium,low] [--cvar NAME=VALUE ...] [--wait 40]

Prints whole-frame GPU ms per scene and tier (mean of the last three 5 s windows of the run) and the number of GL error
lines; logs stay in OUT_DIR/<tier>_<scene>_<hour>/logs. Compare two runs of this script (for example with and without
a --cvar) to cost a change; single-feature differences below about 0.15 ms are inside the run-to-run noise.
"""
import argparse, os, re, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture

SCENES = [("termina_field", 12.0), ("termina_field", 17.5), ("south_clock_town", 10.0), ("north_clock_town", 10.0),
          ("great_bay_coast", 12.0), ("stock_pot_inn", 23.0), ("woodfall_temple", 12.0), ("ikana_canyon", 12.0)]
TIERS = ["ultra", "high", "medium", "low", "original"]


def measure(out, tier, scene, hour, extra, wait, mods):
    cvars = [f"{k}={v}" for k, v in capture.preset_cvars(TIERS.index(tier))]
    cvars += ["gEnhancements.Mods.AlternateAssets=1", "gNewDawn.Perf.Overlay=1", "gNewDawn.PostFx.Debug.Stats=1",
              "gSettings.VsyncEnabled=0", "gMatchRefreshRate=0", "gInterpolationFPS=60",
              f"gNewDawn.Debug.BootHour={hour:.1f}"] + extra
    run = out / f"{tier}_{scene}_{hour:g}"
    cmd = ["nd-heavy", "game", "--", sys.executable, str(Path(__file__).with_name("capture.py")), "--bin-dir", "build/mm",
           "--mods", mods, "--points", scene, "--wait", str(wait), "--out", str(run)]
    for c in cvars:
        cmd += ["--cvar", c]
    subprocess.run(cmd, env=dict(os.environ, MESA_DEBUG="context"), capture_output=True, text=True)
    log = run / "logs" / f"{scene}.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    for png in run.glob("*.png"):  # the frame is not the product here; keep the folder small
        png.unlink()
    gpu = [float(v) for v in re.findall(r"whole frame averages ([\d.]+) ms GPU", text)][-3:]
    errors = sum(("GL error" in line) or ("Mesa: error" in line) or ("GL_INVALID" in line) for line in text.splitlines())
    return (sum(gpu) / len(gpu) if gpu else float("nan")), errors


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--tiers", default="ultra,high,medium,low")
    ap.add_argument("--cvar", action="append", default=[])
    ap.add_argument("--wait", type=float, default=40.0)
    ap.add_argument("--mods", default=os.path.expanduser("~/dev/ghm-mods/sets/community"))
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    tiers = a.tiers.split(",")
    rows, gl = [], 0
    for scene, hour in SCENES:
        cells = []
        for tier in tiers:
            gpu, errors = measure(out, tier, scene, hour, a.cvar, a.wait, a.mods)
            cells.append(gpu)
            gl += errors
            print(f"  {tier:8} {scene:18} {hour:5.1f}  {gpu:6.2f} ms  GL errors {errors}", flush=True)
        rows.append((f"{scene} {hour:g}", cells))
    lines = [f"{'scene':26}" + "".join(f"{t:>9}" for t in tiers)]
    lines += [f"{name:26}" + "".join(f"{c:9.2f}" for c in cells) for name, cells in rows]
    lines.append(f"{'mean':26}" + "".join(f"{sum(r[1][i] for r in rows) / len(rows):9.2f}" for i in range(len(tiers))))
    lines.append(f"{'worst':26}" + "".join(f"{max(r[1][i] for r in rows):9.2f}" for i in range(len(tiers))))
    lines.append(f"GL error lines: {gl}")
    (out / "gpu.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
