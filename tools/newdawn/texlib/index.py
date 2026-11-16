#!/usr/bin/env python3
"""Pull the public catalogs of the shippable sources into one normalised index.

usage: index.py WORKDIR     (WORKDIR = ~/dev/ghm-mods/work/texlib; writes WORKDIR/index/*.json)

Only sources whose terms explicitly allow redistribution are indexed (see SOURCES.md).
Network only; uses each site's public API/list pages, descriptive User-Agent, no auth tricks.
"""
import json, re, sys, time, urllib.request
from pathlib import Path

UA = "newdawn-texlib/1.0 (texture catalog build; CC0 sources only)"


def get(url, raw=False):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=120) as r:
        b = r.read()
    return b if raw else json.loads(b)


def ambientcg():
    out, off = [], 0
    while True:
        d = get(f"https://ambientcg.com/api/v2/full_json?type=Material&limit=1000&offset={off}"
                "&sort=Popular&include=tagData,imageData,downloadData")
        for a in d["foundAssets"]:
            try:
                dl = a["downloadFolders"]["default"]["downloadFiletypeCategories"]["zip"]["downloads"]
            except (TypeError, KeyError):  # no files (not released yet)
                continue
            z = next((x for x in dl if x["attribute"] == "2K-JPG"), None)
            if not z:
                continue
            out.append(dict(source="ambientCG", id=a["assetId"], name=a["displayName"], page=a["shortLink"],
                            cat=a["displayCategory"], tags=a["tags"], method=a["creationMethod"],
                            thumb=a["previewImage"]["256-JPG-242424"], dl=z["fullDownloadPath"], bytes=z["size"],
                            lic="CC0-1.0"))
        if len(d["foundAssets"]) < 1000:
            return out
        off += 1000


def polyhaven():
    out = []
    for k, a in get("https://api.polyhaven.com/assets?t=textures").items():
        out.append(dict(source="PolyHaven", id=k, name=a["name"], page=f"https://polyhaven.com/a/{k}",
                        cat=a["category"], tags=a["tags"] + a["categories"], method="photo",
                        thumb=a["thumbnail_url"], dl=f"https://api.polyhaven.com/files/{k}", bytes=None,
                        lic="CC0-1.0"))
    return out


def gpuopen():
    out = []
    cats = {c["id"]: c["title"] for c in get("https://api.matlib.gpuopen.com/api/categories/?limit=100")["results"]}
    for m in get("https://api.matlib.gpuopen.com/api/materials/?limit=1000")["results"]:
        out.append(dict(source="GPUOpen", id=m["id"], name=m["title"],
                        page="https://matlib.gpuopen.com/main/materials/all?material=" + m["mtlx_material_name"],
                        cat=cats.get(m["category"], ""), tags=[], method="procedural",
                        thumb=(f"https://api.matlib.gpuopen.com/api/renders/{m['renders_order'][0]}/download_thumbnail/"
                               if m.get("renders_order") else None), dl="packages:" + m["id"], bytes=None, lic="MIT"))
    return out


def cgbookcase():
    # metadata only: their download goes through a hotlink-protected CDN, so files are manual-drop (see README)
    out = []
    for t in get("https://www.cgbookcase.com/api/textures"):
        slug = re.sub(r"[^a-z0-9]+", "-", t["title"].lower()).strip("-")
        out.append(dict(source="cgbookcase", id=slug, name=t["title"], page=f"https://www.cgbookcase.com/textures/{slug}",
                        cat=",".join(t["categories"]), tags=t["tags"], method=t["oneType"],
                        thumb=None, dl="manual", bytes=None, lic="CC0-1.0"))
    return out


def texturecan():
    out, seen = [], set()
    for c in "Bricks Concrete Fabrics Ground Marble Metal Rock Tiles Wood Others Plants Paper".split():
        for p in range(1, 12):
            url = f"https://www.texturecan.com/category/{c}/" + (f"{p}/" if p > 1 else "")
            try:
                h = get(url, raw=True).decode("utf8", "ignore")
            except Exception:
                break
            new = 0
            for m in re.finditer(r'<a href="(/details/\d+/)">([^<]*)<br />\(([^)]*)\)</a></div>\s*<div class="texture-img">'
                                 r'<a [^>]*><img src="([^"]+)"', h):
                if m[1] in seen:
                    continue
                seen.add(m[1]); new += 1
                out.append(dict(source="TextureCan", id=m[3].lower().replace(" ", "_"), name=m[2], page="https://www.texturecan.com" + m[1],
                                cat=c, tags=[], method="procedural", thumb="https://www.texturecan.com" + m[4],
                                dl="detail-page", bytes=None, lic="CC0-1.0"))
            time.sleep(0.4)
            if not new:
                break
    return out


if __name__ == "__main__":
    w = Path(sys.argv[1]) / "index"
    w.mkdir(parents=True, exist_ok=True)
    for f in (ambientcg, polyhaven, gpuopen, cgbookcase, texturecan):
        d = f()
        (w / f"{f.__name__}.json").write_text(json.dumps(d, indent=0))
        print(f.__name__, len(d))
