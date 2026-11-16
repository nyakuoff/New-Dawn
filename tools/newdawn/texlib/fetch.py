#!/usr/bin/env python3
"""Download catalog entries (2K sources only) into WORKDIR/sets/ and pin their SHA-256 in catalog.json.

usage: fetch.py WORKDIR [tier ...]      default tier: shortlist.  Re-running verifies against pinned hashes.
Files are untrusted data: they are only stored and hashed here, never executed or extracted into the repo.
Manual sources (download == "manual", e.g. cgbookcase) and the drop-in folder are handled by pin_dropins.py-style
hashing: put the files in WORKDIR/sets/<source>_<id>/ yourself and re-run; they are hashed in place.
"""
import hashlib, json, re, sys, time, urllib.request
from pathlib import Path

HERE = Path(__file__).parent
UA = "newdawn-texlib/1.0 (texture catalog build; CC0/MIT sources only)"
PH_MAPS = {"Diffuse": "diff", "nor_gl": "nor_gl", "Rough": "rough", "AO": "ao", "Displacement": "disp"}


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=300).read()


def sources(e):
    """-> [(filename, url)]"""
    if e["source"] == "ambientCG":
        return [(e["download"].rsplit("=", 1)[1], e["download"])]
    if e["source"] == "PolyHaven":
        f = json.loads(get(e["download"]))
        return [(f"{e['id']}_{short}_2k.jpg", f[k]["2k"]["jpg"]["url"]) for k, short in PH_MAPS.items() if k in f and "2k" in f[k]]
    if e["source"] == "TextureCan":
        h = get(e["page"]).decode("utf8", "ignore")
        m = re.search(r'href="(/downloads/[^"]+_2k_[^"]+\.zip)"', h)
        return [(m[1].rsplit("/", 1)[1], "https://www.texturecan.com" + m[1])] if m else []
    if e["source"] == "GPUOpen":
        pk = [json.loads(get(f"https://api.matlib.gpuopen.com/api/packages/{p}/")) for p in
              json.loads(get(f"https://api.matlib.gpuopen.com/api/materials/{e['id']}/"))["packages"]]
        p = next((p for p in pk if p["label"].startswith("2k")), None)
        return [(p["file"], p["file_url"])] if p else []
    return []


def main(work, tiers):
    cat = json.load(open(HERE / "catalog.json"))
    for e in cat:
        if e["tier"] not in tiers:
            continue
        d = Path(work) / "sets" / f"{e['source']}_{e['id']}"
        d.mkdir(parents=True, exist_ok=True)
        files = []
        for name, url in (sources(e) if e["download"] != "manual" else []) or []:
            f = d / name
            if not f.exists():
                f.write_bytes(get(url)); time.sleep(0.3)
            files.append(dict(name=name, url=url, bytes=f.stat().st_size, sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
        if e["download"] == "manual":   # drop-in: hash whatever the maintainer put there
            files = [dict(name=f.name, url=None, bytes=f.stat().st_size, sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in sorted(d.glob("*")) if f.is_file()]
        old = {x["name"]: x["sha256"] for x in e.get("files") or []}
        for x in files:
            if x["name"] in old and old[x["name"]] != x["sha256"]:
                print("HASH CHANGED", e["id"], x["name"])
        e["files"] = files
        e["sha256"] = hashlib.sha256("".join(x["sha256"] for x in files).encode()).hexdigest() if files else None
        print(e["id"], len(files), sum(x["bytes"] for x in files) // 1000, "kB", flush=True)
        json.dump(cat, open(HERE / "catalog.json", "w"), indent=1)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:] or ["shortlist"])
