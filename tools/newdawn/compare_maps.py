#!/usr/bin/env python3
"""Regression check for the in-game generator (mm/2s2h/NewDawn/RuntimeMaterials.cpp) against the Python tools.

Build the reference with materials.py / recolor.py (see README.md), let the game generate its own cache, then:

  python3 tools/newdawn/compare_maps.py --cpp <app dir>/cache/newdawn-materials/live \\
      --materials zz-newdawn-materials.o2r --recolor zy-newdawn-recolor.o2r

Prints, per map type, the largest and mean absolute channel error, the share of bytes off by more than one level,
and how many archive entries are byte-identical. Reads game-derived archives only; nothing here is committed."""
import argparse
import json
import struct
import zipfile

import numpy as np


def pixels(entry):
    return np.frombuffer(entry, dtype=np.uint8, offset=92)  # 64-byte header + 28-byte TextureV1 body


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cpp", required=True, help="directory with the game's newdawn-materials.o2r / newdawn-recolor.o2r")
    ap.add_argument("--materials", required=True, help="Python-built materials archive")
    ap.add_argument("--recolor", help="Python-built recolour archive")
    a = ap.parse_args()
    ref = zipfile.ZipFile(a.materials)
    new = zipfile.ZipFile(a.cpp + "/newdawn-materials.o2r")
    rm = {e["resource"]: e for e in json.loads(ref.read("newdawn-materials.json"))["materials"]}
    nm = {e["resource"]: e for e in json.loads(new.read("newdawn-materials.json"))["materials"]}
    print(f"materials: python {len(rm)}, game {len(nm)}, common {len(rm.keys() & nm.keys())}")
    keys = ("source_pixels_fnv1a64", "source_size", "output_size", "source_h_byte_scale", "source_v_pixel_scale")
    bad = [r for r in rm.keys() & nm.keys() if any(rm[r][k] != nm[r][k] for k in keys)]
    print(f"metadata the renderer checks differs for {len(bad)} textures")

    channels = {"normal X": [], "normal Y": [], "roughness": [], "relief": []}
    identical = total = 0
    for r in sorted(rm.keys() & nm.keys()):
        for suffix, names in (("_n", ("normal X", "normal Y")), ("_r", ("roughness", "relief"))):
            x, y = ref.read("alt/" + r + suffix), new.read("alt/" + r + suffix)
            total += 1
            identical += x == y
            if x != y:
                px, py = pixels(x).astype(int), pixels(y).astype(int)
                for i, name in enumerate(names):
                    d = np.abs(px[i::2] - py[i::2])
                    channels[name].append((d.max(), d.sum(), (d > 1).sum(), d.size))
    print(f"map entries byte-identical: {identical} of {total}")
    for name, rows in channels.items():
        if rows:
            print(f"  {name:10s} differing entries {len(rows)}, max error {max(r[0] for r in rows)}, "
                  f"bytes off by >1: {sum(r[2] for r in rows)}")
    if a.recolor:
        ref, new = zipfile.ZipFile(a.recolor), zipfile.ZipFile(a.cpp + "/newdawn-recolor.o2r")
        names = [n for n in ref.namelist() if n.startswith("alt/")]
        same = sum(ref.read(n) == new.read(n) for n in names if n in new.namelist())
        print(f"recolour entries byte-identical: {same} of {len(names)}")


if __name__ == "__main__":
    main()
