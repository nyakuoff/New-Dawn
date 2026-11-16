#!/usr/bin/env python3
"""Build the shipped base material library (ND-016): 1024x1024 JPEG sets (albedo, normal GL, roughness, height) + manifest.json + MANIFEST.md
in mm/assets/newdawn/materials/.

Two sources, one manifest:
  * library.json            hand picks from ambientCG (downloaded here, licence page checked): the sets the hand-tuned scenes use.
  * --catalog catalog.json  the curated shortlist of tools/newdawn/texlib (sources already downloaded by its fetch.py into --work/sets),
                            only the classes the generator can use as input; per-region tags are kept for scene-based selection.
Downloads are untrusted data: only whitelisted map files are read; run with `python3 -I`.
usage: build_library.py --dl DIR --out mm/assets/newdawn/materials [--catalog tools/newdawn/texlib/catalog.json --work ~/dev/ghm-mods/work/texlib]"""
import argparse, datetime, hashlib, io, json, os, re, sys, urllib.request, zipfile
from PIL import Image, ImageFile

ImageFile.MAXBLOCK = 1 << 26
UA = {'User-Agent': 'newdawn-asset-build/0.2'}
# JPEG settings: about 0.7 MB per set at 1024 px (normals keep full chroma resolution: R and G are the data)
Q = {'albedo': (78, 2), 'normal': (84, 0), 'rough': (68, 0), 'height': (68, 0)}
MODE = {'albedo': 'RGB', 'normal': 'RGB', 'rough': 'L', 'height': 'L'}
FIND = {  # lower-case substrings of the file names, most specific first
    'albedo': ['_diff_', '_color', '_basecolor', '_albedo'],
    'normal': ['_nor_gl_', '_normalgl', '_normal_opengl', '_nor_gl'],
    'rough': ['_rough_', '_roughness'],
    'height': ['_disp_', '_displacement', '_height'],
}
# catalog class -> (manifest class, roles); classes not listed are not imported (brick, cobble, flagstone are complete walls with joints,
# wallpaper/carpet/leather/fabric/moss/grass are weak in the catalog: the generator leaves them native or builds them itself)
CLASS_ROLES = {
    'rock': ('rock', ['rock']), 'worked_stone': ('worked_stone', ['face', 'rock']), 'wood_planks': ('wood_planks', ['planks']),
    'plaster': ('plaster', ['plaster']), 'thatch': ('thatch', ['thatch']), 'wood_beam': ('wood_beam', ['bark', 'beam']),
    'bark': ('bark', ['bark', 'beam']), 'roof_tiles': ('roof_tiles', ['tiles']), 'dirt': ('dirt', ['dirt', 'ground']),
    'gravel': ('gravel', ['gravel', 'mortar', 'ground']), 'sand': ('sand', ['sand', 'ground']), 'ice_snow': ('ice_snow', ['snow']),
    'rope': ('rope', ['rope']), 'mud': ('mud', ['mud', 'ground']), 'metal': ('metal', ['metal']),
}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=180) as r:
        return r.read()


class Source:
    """Map files of one downloaded set: loose files or members of a zip, matched by name."""
    def __init__(self, files):  # name -> callable returning bytes
        self.files = files

    @staticmethod
    def from_dir(d):
        files = {}
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if f.lower().endswith('.zip'):
                z = zipfile.ZipFile(p)
                for n in z.namelist():
                    if not n.endswith('/'):
                        files[os.path.basename(n)] = (lambda z=z, n=n: z.read(n))
            else:
                files[f] = (lambda p=p: open(p, 'rb').read())
        return Source(files)

    @staticmethod
    def from_zip(raw):
        z = zipfile.ZipFile(io.BytesIO(raw))
        return Source({os.path.basename(n): (lambda n=n: z.read(n)) for n in z.namelist() if not n.endswith('/')})

    def find(self, key):
        for pat in FIND[key]:
            for n in sorted(self.files):
                ln = n.lower()
                if pat in ln and ln.endswith(('.jpg', '.jpeg', '.png')) and not (key == 'normal' and 'directx' in ln or '_nor_dx' in ln or 'normaldx' in ln):
                    return n
        return None


def encode(src, mid, out, size):
    files = {}; total = 0
    for key in ('albedo', 'normal', 'rough', 'height'):
        n = src.find(key)
        if n is None:
            print('  missing', mid, key)
            continue
        im = Image.open(io.BytesIO(src.files[n]())).convert(MODE[key]).resize((size, size), Image.LANCZOS)
        fn = f'{mid}.{key}.jpg'
        im.save(f'{out}/{fn}', 'JPEG', quality=Q[key][0], subsampling=Q[key][1], optimize=True)
        files[key] = fn; total += os.path.getsize(f'{out}/{fn}')
    return files, total


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--dl', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--size', type=int, default=1024); ap.add_argument('--list', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'library.json'))
    ap.add_argument('--catalog'); ap.add_argument('--work', default=os.path.expanduser('~/dev/ghm-mods/work/texlib'))
    a = ap.parse_args(); os.makedirs(a.dl, exist_ok=True); os.makedirs(a.out, exist_ok=True)
    for f in os.listdir(a.out):  # the library is rebuilt from scratch
        if f.endswith('.jpg') or f in ('manifest.json', 'MANIFEST.md'):
            os.remove(os.path.join(a.out, f))
    lic = get('https://docs.ambientcg.com/license/').decode('utf8', 'replace')
    assert 'CC0 1.0 Universal' in lic, 'ambientCG licence page no longer states CC0 1.0'
    out = []; total = 0; seen_assets = set()
    for m in json.load(open(a.list))['materials']:
        aid = m['asset']; zp = f'{a.dl}/{aid}_2K-JPG.zip'
        if not os.path.exists(zp):
            open(zp, 'wb').write(get(f'https://ambientcg.com/get?file={aid}_2K-JPG.zip'))
        raw = open(zp, 'rb').read(); info = json.loads(get(f'https://ambientcg.com/api/v2/full_json?id={aid}'))['foundAssets'][0]
        files, t = encode(Source.from_zip(raw), m['id'], a.out, a.size); total += t; seen_assets.add(('ambientCG', aid.lower()))
        regions = m.get('regions') or [m.get('region', 'any')]
        out.append(dict(id=m['id'], asset=aid, **{'class': m['class']}, roles=m['roles'], regions=regions,
                        source=dict(name='ambientCG', asset=aid, page=f'https://ambientcg.com/view?id={aid}', download=f'https://ambientcg.com/get?file={aid}_2K-JPG.zip',
                                    license='CC0-1.0', license_page='https://docs.ambientcg.com/license/', zip_sha256=hashlib.sha256(raw).hexdigest(), fetched=str(datetime.date.today())),
                        files=files, size=a.size, size_cm=[info.get('dimensionX'), info.get('dimensionY')]))
        print(f'{m["id"]:22s} {t / 1e6:5.2f} MB', flush=True)
    if a.catalog:
        cat = json.load(open(a.catalog))
        for e in cat:
            if e['tier'] != 'shortlist' or e['class'] not in CLASS_ROLES:
                continue
            if (e['source'], re.sub(r'_2k.*', '', e['id']).lower()) in seen_assets or e['license'] not in ('CC0-1.0', 'MIT'):
                continue
            d = f"{a.work}/sets/{e['source']}_{e['id']}"
            if not os.path.isdir(d):
                print('  no download for', e['id']); continue
            mid = re.sub(r'[^a-z0-9_]+', '_', e['id'].lower())
            if any(o['id'] == mid for o in out):
                continue
            files, t = encode(Source.from_dir(d), mid, a.out, a.size)
            if len(files) < 3:
                print('  skipped (incomplete)', e['id']); continue
            total += t
            cls, roles = CLASS_ROLES[e['class']]
            out.append(dict(id=mid, asset=e['id'], **{'class': cls}, roles=roles, regions=e['regions'] or ['any'],
                            source=dict(name=e['source'], asset=e['id'], page=e['page'], download=e['download'], license=e['license'], license_page=e['license_page'],
                                        zip_sha256=e['sha256'], catalog='tools/newdawn/texlib/catalog.json', score=e['score']),
                            files=files, size=a.size))
            print(f'{mid:34s} {e["class"]:12s} {t / 1e6:5.2f} MB', flush=True)
    json.dump(dict(version=2, size=a.size, jpeg=Q, note='generated by tools/newdawn/matlib/build_library.py; do not edit', materials=out), open(f'{a.out}/manifest.json', 'w'), indent=1)
    L = ['# New Dawn base material library (ND-016)', '',
         'CC0 (and MIT where stated) PBR materials, re-encoded by `tools/newdawn/matlib/build_library.py` to 1024x1024 JPEG (albedo, normal GL, roughness, height; AO and the 2K originals are not shipped).',
         'Used only as input to the first-run texture generator (`mm/2s2h/NewDawn/RuntimeTextures.cpp`): the player never sees them unmodified.',
         f'Total: {total / 1e6:.1f} MB for {len(out)} sets. Credits and licence texts: `docs/licenses/TEXLIB_CREDITS.md`, `docs/licenses/THIRD_PARTY_ASSETS.md`.', '',
         '| id | class | roles | regions | source | asset | licence | page |', '|---|---|---|---|---|---|---|---|']
    for m in out:
        s = m['source']; L.append(f"| {m['id']} | {m['class']} | {', '.join(m['roles'])} | {', '.join(m['regions'])} | {s['name']} | {s['asset']} | {s['license']} | {s['page']} |")
    L += ['', 'Licence evidence: ambientCG sets via the licence page the tool fetches (the build aborts if it stops saying "CC0 1.0 Universal"); catalog sets via the pinned hashes and licence documents of `tools/newdawn/texlib`.']
    open(f'{a.out}/MANIFEST.md', 'w').write('\n'.join(L) + '\n')
    print(f'total {total / 1e6:.1f} MB, {len(out)} sets')


if __name__ == '__main__':
    main()
