#!/usr/bin/env python3
"""Generate mm/2s2h/NewDawn/Mm3dCharacterTable.inc: the N64 Flex skeletons whose limb hierarchy is identical to a CMB in
the same-named 3DS actor archive (docs/MM3D_CHARACTERS.md). Rows carry names only; no game data is written.

  chartable.py [--o2r mm.o2r] [--romfs RomFS] [--check]    (--check prints the table instead of writing it)

REVIEWED rows are enabled in game; the others convert (and are counted) but stay held back until their models have been
looked at. SKIP lists the CMB material indices of props the N64 draws from separate display lists (bottles, masks...).
"""
import argparse
import collections
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import binary  # noqa: E402
import cmb  # noqa: E402

HOME = Path.home()
OUT = Path(__file__).resolve().parents[3] / 'mm/2s2h/NewDawn/Mm3dCharacterTable.inc'

# object/skeleton -> comma separated CMB material indices to leave out; filled in while reviewing each model.
SKIP = {
    ('object_tru', 'gKoumeSkel'): '5,6',            # the potion bottle (the N64 draws gKoumeBottleDL itself); her broom is a limb
    ('object_sdn', 'gSoldierSkel'): '2,3',
    ('object_an1', 'gAnju1Skel'): '0,1',
    ('object_rsn', 'gBombShopkeeperSkel'): '2,3,4',
}
# Never converted, even with "Include unreviewed 3DS models": wrong or unverified in game (docs/MM3D_CHARACTERS.md).
EXCLUDE = {
    ('object_bigokuta', 'gBigOctoSkel'),               # draws translucent and in a different shape
    ('object_harfgibud', 'gPamelasFatherGibdoSkel'),   # the archive only has the human model
    ('object_dnq', 'gDekuPrincessSkel'),               # not the Deku Princess
    ('object_boss05', 'gBioDekuBabaLilyPadSkel'),      # a flat placeholder
    ('object_mm', 'object_mm_Skel_0096E8'),            # Postman: identical frames, cause unknown
    ('object_ik', 'gIronKnuckleSkel'),                 # keeps limb display lists to draw flying armour later
    ('object_thiefbird', 'gTakkuriSkel'),              # same: the death pieces reuse the limb display lists
    ('object_kz', 'gFighterPirateSkel'),               # weapons not compared with the N64 swords
    ('object_dy_obj', 'gGreatFairySkel'),              # one skeleton, five colour models: the model cannot follow her param
}
# Skeletons whose archive holds several CMBs with the same hierarchy (variants of one character): the one to use.
CMB = {
    ('object_bg', 'gTreasureChestShopGalSkel'): 'model/treasure_girl.cmb',   # purple hair, pink top (the receptionist is reception_girl)
    ('object_wf', 'gWolfosWhiteSkel'): 'model/wolfos_white.cmb',
    ('object_jso', 'gGaroSkel'): 'model/garo.cmb',                           # gGaroMasterSkel keeps boss_garo.cmb
}
# Per-model colour overrides (light reference, saturation); 0 keeps the global defaults (gNewDawn.MM3D.LightRef / .Saturation).
# Set where a model's own palette is already saturated (checked 1:1 against the N64 model with the 4K pack).
COLOUR = {
    ('object_sdn', 'gSoldierSkel'): (0.5, 1.0),         # yellow skin and orange trim were garish at 1.35
    ('object_bg', 'object_bg_Skel_011B60'): (0.55, 1.05),  # the face turned orange
}
# object/skeleton pairs that have been looked at in game (colour, props, joints) and may be switched on.
REVIEWED = {
    ('object_sdn', 'gSoldierSkel'),               # Clock Town guard
    ('object_an1', 'gAnju1Skel'),                 # Anju
    ('object_bg', 'object_bg_Skel_011B60'),       # Mayor's Residence receptionist
    ('object_dnt', 'gBusinessScrubSkel'),         # Business Scrub
    ('object_bg', 'gTreasureChestShopGalSkel'),   # Treasure Chest Shop gal (treasure_girl.cmb: purple hair, pink top)
    ('object_ani', 'gAniSkel'),                   # the man climbing the tree in Termina Field
}


def string(d, p):
    n = struct.unpack_from('<I', d, p)[0]
    return d[p + 4:p + 4 + n].decode(), p + 4 + n


def read_parents(z, path):
    """-> parent index per limb for a Flex skeleton resource, or None."""
    d = z.read(path)
    if d[4:8] != b'LKSO' or d[64] != 1:
        return None
    count = struct.unpack_from('<I', d, 64 + 11)[0]
    p, table = 64 + 15, []
    for _ in range(count):
        s, p = string(d, p)
        table.append(s)
    child, sibling = [], []
    for s in table:
        b = z.read(s)
        p = 64 + 2
        _, p = string(b, p)
        p += 2
        if struct.unpack_from('<I', b, p)[0]:
            return None
        p += 4
        _, p = string(b, p)
        p += 18
        for _ in range(4):
            _, p = string(b, p)
        c, sb = struct.unpack_from('<BB', b, p + 6)
        child.append(c)
        sibling.append(sb)
    parent = [-1] * count
    for i, c in enumerate(child):
        while c != 255:
            parent[c] = i
            c = sibling[c]
    return parent


def shape(parent):
    kids = collections.defaultdict(list)
    for i, p in enumerate(parent):
        kids[p].append(i)

    def canon(i):
        return '(' + ''.join(sorted(canon(c) for c in kids[i])) + ')'
    return ''.join(sorted(canon(r) for r in kids[-1]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--o2r', default=str(HOME / 'dev/2ship2harkinian/build-cmake/mm/mm.o2r'))
    ap.add_argument('--romfs', default=str(HOME / 'dev/ghm-mods/mm3d/romfs'))
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    z = zipfile.ZipFile(a.o2r)
    cache, rows = {}, []
    for name in sorted(z.namelist()):
        parts = name.split('/')
        if len(parts) != 3 or parts[0] != 'objects' or not parts[1].startswith('object_'):
            continue
        if not (parts[2].endswith('Skel') or '_Skel_' in parts[2]) or z.getinfo(name).file_size > 4000:
            continue
        parent = read_parents(z, name)
        if not parent or len(parent) < 3:
            continue
        obj, skel = parts[1], parts[2]
        if (obj, skel) in EXCLUDE:
            continue
        arch = 'zelda2_' + obj[len('object_'):]
        path = Path(a.romfs, 'actors', arch + '.gar.lzs')
        if not path.exists():
            continue
        if arch not in cache:
            cache[arch] = binary.gar(path.read_bytes())
        want, best = shape(parent), None
        for key, data in cache[arch].items():
            if bytes(data[:4]) != b'cmb ':
                continue
            try:
                m = cmb.parse(data, decode_textures=False)
            except Exception:  # noqa: BLE001
                continue
            ids = {b['id']: i for i, b in enumerate(m['bones'])}
            par = [ids.get(b['parent'], -1) if b['parent'] >= 0 else -1 for b in m['bones']]
            tris = len(m['indices']) // 3
            if shape(par) == want and (best is None or tris > best[1]):
                best = (key, tris)
        if best and (obj, skel) in CMB:
            tris = [len(cmb.parse(cache[arch][CMB[obj, skel]], decode_textures=False)['indices']) // 3]
            best = (CMB[obj, skel], tris[0])
        if best:
            rows.append((obj, skel, arch, best[0], len(parent), best[1]))
    lines = ['// Generated by tools/newdawn/mm3d/chartable.py: N64 Flex skeleton, 3DS actor archive and CMB with the same limb',
             '// hierarchy, props to leave out (material indices) and whether the model has been reviewed in game.',
             '// {object, skeleton, archive (zelda2_*), cmb, skip materials, reviewed, light reference, saturation}']
    for obj, skel, arch, cm, limbs, tris in rows:
        lr, sat = COLOUR.get((obj, skel), (0, 0))
        lines.append('{ "%s", "%s", "%s", "%s", "%s", %s, %.2ff, %.2ff },  // %d limbs, %d triangles' % (
            obj, skel, arch, cm, SKIP.get((obj, skel), ''), 'true' if (obj, skel) in REVIEWED else 'false', lr, sat, limbs, tris))
    text = '\n'.join(lines) + '\n'
    if a.check:
        print(text)
    else:
        OUT.write_text(text)
    print(f'{len(rows)} skeletons', file=sys.stderr)


if __name__ == '__main__':
    main()
