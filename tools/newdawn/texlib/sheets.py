#!/usr/bin/env python3
"""Download preview thumbnails of the pool and build one contact sheet per class.

usage: sheets.py WORKDIR OUTDIR dl        (thumbnails only, light network)
       nd-heavy tool -- sheets.py WORKDIR OUTDIR   (montage; needs ImageMagick)
Label under each tile: id / source / auto score. Sheets are 6 columns, 24 tiles per sheet.
"""
import json, os, subprocess, sys, urllib.request
from collections import defaultdict
from pathlib import Path

UA = "newdawn-texlib/1.0 (texture catalog build; CC0 sources only)"
w, out = Path(sys.argv[1]), Path(sys.argv[2])
(w / "thumbs").mkdir(exist_ok=True); out.mkdir(parents=True, exist_ok=True)
DL = len(sys.argv) > 3
pool = json.load(open(w / os.environ.get("POOL", "pool.json")))
for m in pool:
    f = w / "thumbs" / f"{m['source']}_{m['id']}.img".replace("/", "_")
    m["file"] = str(f)
    if not f.exists() and DL:
        try:
            f.write_bytes(urllib.request.urlopen(urllib.request.Request(m["thumb"], headers={"User-Agent": UA}), timeout=60).read())
        except Exception as e:
            print("thumb fail", m["id"], e); m["file"] = None
if DL:
    sys.exit()
by = defaultdict(list)
for m in pool:
    if m["file"] and Path(m["file"]).exists():
        by[m["cls"]].append(m)
for cls, ms in by.items():
    ms.sort(key=lambda m: (m["source"], m["id"]))
    for i in range(0, len(ms), 24):
        part = ms[i:i + 24]
        args = ["magick", "montage", "-background", "#202020", "-fill", "white", "-pointsize", "13", "-tile", "6x", "-geometry", "256x256+4+4"]
        for m in part:
            args += ["-label", f"{m['id'][:22]} {m['source'][:4]} {m['auto']}", f"{m['file']}[0]"]
        args.append(str(out / f"{cls}_{i // 24 + 1}.jpg"))
        subprocess.run(args, check=False)
print("sheets in", out)
