#!/usr/bin/env python3
"""Build catalog.json from the indexes + the visual triage scores.

usage: build_catalog.py WORKDIR     (needs index/*.json from index.py; writes tools/newdawn/texlib/catalog.json)
triage.txt lines: `id score [class]`  (score 0-10 from looking at the contact sheets; 0 = drop;
an id of >=20 chars matches by prefix because the sheet labels truncate, GPUOpen ids by any prefix).
Tiers: rejected <6, reserve 6-7.5, candidate >=7.5, shortlist = best scores >=8 with a per-class cap, then topped up so every region has >=3 per major class group.
"""
import collections, glob, json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import classify

HERE = Path(__file__).parent
CLASS_CAP = 5          # max shortlisted per class
CAND_MIN = 7.5
SHORT_MIN = 8.0        # score floor for the shortlist
LIC_PAGE = {"CC0-1.0": "https://creativecommons.org/publicdomain/zero/1.0/", "MIT": "https://opensource.org/license/mit"}
LIC_DOC = {"ambientCG": "docs/licenses/texlib/ambientcg.txt", "PolyHaven": "docs/licenses/texlib/polyhaven.txt",
           "TextureCan": "docs/licenses/texlib/texturecan.txt", "GPUOpen": "docs/licenses/texlib/gpuopen-mit.txt"}


def load_triage():
    t = {}
    for l in open(HERE / "triage.txt"):
        p = l.split()
        if p and not l.startswith("#"):
            t[p[0]] = (float(p[1]), p[2] if len(p) > 2 else None)
    return t


def main(work):
    allm = [m for f in sorted(glob.glob(f"{work}/index/*.json")) for m in json.load(open(f))]
    by_id = {m["id"]: m for m in allm}
    hit = {}
    for k, (score, cls) in load_triage().items():
        ms = [by_id[k]] if k in by_id else [m for m in allm if m["id"].startswith(k) and (len(k) >= 20 or m["source"] == "GPUOpen")]
        if not ms:
            print("triage key without match:", k)
        for m in ms:
            hit[m["id"]] = (m, score, cls)
    pinned = {e["id"]: e for e in json.load(open(HERE / "catalog.json"))} if (HERE / "catalog.json").exists() else {}
    out = []
    for m, score, cls in hit.values():
        c = classify.classify(m) or dict(m, cls="rock", regions=[], auto=5)
        c["cls"] = cls or c["cls"]
        c["regions"] = [r for r, cl, rx in classify.REGIONS if c["cls"] in cl and (rx is None or classify.re.search(rx, " ".join([m["name"], m["cat"], " ".join(m["tags"])]).lower()))] or ["dungeon"]
        out.append(dict(id=m["id"], source=m["source"], name=m["name"], page=m["page"], license=m["lic"],
                        license_page=LIC_PAGE[m["lic"]], license_doc=LIC_DOC[m["source"]], download=m["dl"],
                        sha256=pinned.get(m["id"], {}).get("sha256"), **{"class": c["cls"]}, regions=c["regions"], score=score,
                        files=pinned.get(m["id"], {}).get("files"), method=m["method"], tier="rejected" if score < 6 else "reserve" if score < CAND_MIN else "candidate"))
    out.sort(key=lambda e: -e["score"])
    cnt = collections.Counter()
    for e in out:
        if e["score"] >= SHORT_MIN and cnt[e["class"]] < CLASS_CAP:
            e["tier"] = "shortlist"; cnt[e["class"]] += 1
    # top up: every region gets >=3 shortlisted per big class group
    groups = {"stone": {"brick", "cobble", "flagstone", "rock", "worked_stone"}, "wood": {"wood_planks", "wood_beam", "bark"},
              "ground": {"dirt", "sand", "gravel", "mud", "grass", "moss", "ice_snow"}, "surface": {"plaster", "roof_tiles", "thatch", "metal", "fabric", "leather", "carpet_rug", "wallpaper", "rope"}}
    for r in {r for e in out for r in e["regions"]}:
        for g, cl in groups.items():
            have = [e for e in out if e["tier"] == "shortlist" and r in e["regions"] and e["class"] in cl]
            for e in [e for e in out if e["tier"] == "candidate" and r in e["regions"] and e["class"] in cl and e["score"] >= 7][:max(0, 3 - len(have))]:
                e["tier"] = "shortlist"
    # utility classes (little good material exists) still get their best 3 so the engine has something to pick
    for cl in ("grass", "moss", "metal", "fabric", "leather", "mud", "carpet_rug", "wallpaper", "rope", "thatch"):
        have = sum(1 for e in out if e["class"] == cl and e["tier"] == "shortlist")
        for e in [e for e in out if e["class"] == cl and e["tier"] in ("candidate", "reserve") and e["score"] >= 6.5][:max(0, 3 - have)]:
            e["tier"] = "shortlist"
    # download tiers (sizes measured 2026-11-16: WebP colour+normal+roughness = 0.67 MB/material at 1K, 2.6 MB at 2K)
    # core = best of every class + 2nd best of the six biggest (~30, ND-016 "about 30"); base = whole shortlist at 1K; hi = top 30 also at 2K
    sl = [e for e in out if e["tier"] == "shortlist"]
    seen = collections.Counter()
    for e in sl:   # `out` is sorted by score desc
        seen[e["class"]] += 1
        e["ship"] = "core" if seen[e["class"]] == 1 or (seen[e["class"]] == 2 and e["class"] in ("rock", "worked_stone", "wood_planks", "plaster", "dirt", "brick")) else "base"
    for e in sl[:30]:
        e["hi_2k"] = True
    json.dump(out, open(HERE / "catalog.json", "w"), indent=1)
    t = collections.Counter(e["tier"] for e in out)
    print(len(out), dict(t))
    print("class x shortlist:", dict(collections.Counter(e["class"] for e in out if e["tier"] == "shortlist")))
    print("region x candidates:", dict(collections.Counter(r for e in out if e["tier"] in ("candidate", "shortlist") for r in e["regions"])))


if __name__ == "__main__":
    main(sys.argv[1])
