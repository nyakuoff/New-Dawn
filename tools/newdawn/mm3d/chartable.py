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
# Human Link: 3DS draws that are variants or equipment (hand poses 13-29, bow 30-33, shield 34, stick 35, second face 37, second
# hair 39, shadow 41, hookshot and everything after 44), duplicates and unknown blobs (1, 2, 7).
_LINK_DRAWS = [1, 2, 7, 35, 37, 39, 41] + list(range(13, 35)) + list(range(44, 95))
SKIP = {
    # Deku Link: hands (16-19) and the bottle and cork (12, 23, 24) stay N64, d10 is a stray triangle, d20 the shell (link_nuts_shiel).
    ('object_link_nuts', 'gLinkDekuSkel'): 'd10,d12,d16,d17,d18,d19,d20,d23,d24,k15,k18,k19,o22=300,u5,s13=0.75@12,s14=0.75@12',
    ('object_link_child', 'gLinkHumanSkel'): ','.join(['d%d' % d for d in _LINK_DRAWS] + ['k15', 'k18', 'k19', 's5=0.66:0.85@12']),
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
# Skeletons whose 3DS rig is not the N64 one: (archive, CMB, "bone:limb,...") from the proportions and the joint offsets of both
# (docs/MM3D_CHARACTERS.md, rig reduction). Unlisted 3DS bones follow their nearest listed ancestor; unlisted N64 limbs draw nothing.
RIG = {
    # Deku Link (nuts archive): the same layout again (the 3DS offsets differ, the recipe does not).
    ('object_link_nuts', 'gLinkDekuSkel'): ('zelda2_link_nuts_new', 'nuts/model/link_deknuts.cmb',
        '1:0,2:1,3:6,4:7,5:8,6:3,7:4,8:5,9:9>20,11:10,12:11,15:13,16:14,17:15,19:16,20:17,21:18,22:19'),
    # Human Link (child archive): the same 21-limb layout and the same 3DS rig as Kafei's (its bones 23 to 25 only carry the
    # shadow and the deku stick). Hands, sheath and equipment keep their N64 display lists for now (SKIP k15,k18,k19).
    ('object_link_child', 'gLinkHumanSkel'): ('zelda2_link_child_new', 'child/model/link_child.cmb',
        '1:0,2:1,3:6,4:7,5:8,6:3,7:4,8:5,9:9>20,11:10,12:11,15:13,16:14,17:15,19:16,20:17,21:18,22:19'),
    # Bombers (cs): the 3DS right arm has a wrist and a hand bone where the N64 has a pivot limb with two co-located parts
    ('object_cs', 'gBomberSkel'): ('zelda2_cs', 'model/bombers.cmb',
        '0:0,1:1,2:2,3:3,4:4,5:5,6:6,7:7,10:8,11:9,12:10,13:11,14:12,15:15,16:14,8:18,9:17'),
    # Romani (ma1): clavicles fold into the torso, the skirt's bones fold into its first (their rest rotations are not the N64
    # dress limbs'; pairing them flared the skirt), the head's second bone folds into the first
    ('object_ma1', 'gRomaniSkel'): ('zelda2_ma1', 'model/romany.cmb',
        '0:0,1:1,2:2,3:3,4:4,5:5,6:6,7:7,8:8,11:12,12:13,14:14,15:15,17:16,18:17,19:18,22:19,23:20,24:21'),
    # Cremia (ma2)
    ('object_ma2', 'gCremiaSkel'): ('zelda2_ma2', 'model/crimea.cmb',
        '0:0,1:10,2:11,3:12,4:13,5:14,6:15,7:16,8:17,11:1,12:8,14:9,16:2,17:3,18:4,20:5,21:6,22:7'),
    # Kafei (test3): the 3DS hips and chest are single bones, the N64 has pivot limbs (root, lower and upper root) above them
    ('object_test3', 'gKafeiSkel'): ('zelda2_kerfay', 'model/kafai.cmb',
        '1:0,2:1,3:6,4:7,5:8,6:3,7:4,8:5,9:9>20,11:10,12:11,15:13,16:14,17:15,19:16,20:17,21:18,22:19'),
}
# Eye and mouth states (docs/MM3D_CHARACTERS.md, faces): "E|M : segment the N64 head display list reads : the 3DS CMAB : N64 state
# texture = 3DS frame, ...", parts joined by "|". The frames were matched to the N64 states by eye, in montages of both.
FACE = {
    # Human Link: the 3DS frames follow the N64 enum (open, half, closed, right, left, up, down, wincing; closed, half, open, smile).
    ('object_link_child', 'gLinkHumanSkel'): 'E:8:child/misc/link_child_eye.cmab:gLinkHumanEyesOpenTex=0,gLinkHumanEyesHalfTex=1,'
        'gLinkHumanEyesClosedTex=2,gLinkHumanEyesRightTex=3,gLinkHumanEyesLeftTex=4,gLinkHumanEyesUpTex=5,gLinkHumanEyesDownTex=6,'
        'gLinkHumanEyesWincingTex=7|M:9:child/misc/link_child_mouth.cmab:gLinkHumanMouthClosedTex=0,gLinkHumanMouthHalfTex=1,'
        'gLinkHumanMouthOpenTex=2,gLinkHumanMouthSmileTex=3',
    ('object_an1', 'gAnju1Skel'): 'E:8:misc/ange_eye.cmab:gAnju1EyeOpenTex=0,gAnju1EyeHalfTex=1,gAnju1EyeClosedTex=2,'
        'gAnju1EyeComfortTex=3,gAnju1EyeSadTex=4,gAnju1EyeRelievedClosedTex=5'
        '|M:9:misc/ange_mouth.cmab:gAnju1MouthClosedTex=0,gAnju1MouthHappyTex=1,gAnju1MouthOpenTex=2',
    ('object_ani', 'gAniSkel'): 'E:8:misc/parttimeman_eye.cmab:gAniOpenEyeTex=0,gAniClosingEyeTex=1,gAniClosedEyeTex=2',
    ('object_bg', 'object_bg_Skel_011B60'): 'E:8:misc/reception_girl_eye.cmab:object_bg_Tex_00F8F0=0,object_bg_Tex_00FCF0=1,'
        'object_bg_Tex_0100F0=2',
    ('object_bg', 'gTreasureChestShopGalSkel'): 'E:8:misc/treasure_girl_eye.cmab:gTreasureChestShopGalEyeOpenDownTex=0,'
        'gTreasureChestShopGalEyeHalfDownTex=1,gTreasureChestShopGalEyeClosedTex=2,gTreasureChestShopGalEyeOpenUpTex=3,'
        'gTreasureChestShopGalEyeHalfUpTex=4',
    ('object_ma1', 'gRomaniSkel'): 'E:8:misc/romany_eye.cmab:gRomaniEyeOpenTex=0,gRomaniEyeHalfTex=1,gRomaniEyeClosedTex=2,'
        'gRomaniEyeHappyTex=3,gRomaniEyeSadTex=4'
        '|M:9:misc/romany_mouth.cmab:gRomaniMouthFrownTex=1,gRomaniMouthHangingOpenTex=3,gRomaniMouthHappyTex=2,gRomaniMouthSmileTex=3',
    ('object_ma2', 'gCremiaSkel'): 'E:8:misc/crimea_eye.cmab:gCremiaEyeAngryTex=0,gCremiaEyeHalfTex=1,gCremiaEyeClosedTex=2,'
        'gCremiaEyeHappyTex=3,gCremiaEyeOpenTex=4,gCremiaEyeSadTex=5'
        '|M:9:misc/crimea_mouth.cmab:gCremiaMouthFrownTex=1,gCremiaMouthHangingOpenTex=1,gCremiaMouthNormalTex=2,'
        'gCremiaMouthSlightSmileTex=3',
    ('object_cs', 'gBomberSkel'): 'E:8:misc/bombers_eye.cmab:gBomberEyeOpenTex=0,gBomberEyeHalfTex=1,gBomberEyeClosedTex=2',
    ('object_test3', 'gKafeiSkel'): 'E:8:misc/kafai_eye.cmab:gKafeiEyesOpenTex=0,gKafeiEyesDownTex=1,gKafeiEyesHalfTex=1,'
        'gKafeiEyesClosedTex=2,gKafeiEyesLeftTex=3,gKafeiEyesUpTex=4,gKafeiEyesRightTex=5,gKafeiEyesWincingTex=6'
        '|M:9:misc/kafai_mouth.cmab:gKafeiMouthClosedTex=0,gKafeiMouthHalfTex=1,gKafeiMouthOpenTex=2,gKafeiMouthSmileTex=3',
}
# Per-material hue and saturation (after the colour mapping), "material:hue degrees:saturation factor". The 3DS hair reads hotter than
# the N64's (hue towards orange-red); the CMB material of the hair texture is looked up in docs/MM3D_CHARACTERS.md.
TINT = {
    # hair: ginger towards the N64 blond; tunic and hat greens (material 1, hue 60 to 180 only, the buckle and boots share the texture):
    # a deeper, less saturated forest green (the N64 tunic measures value 0.32 in Clock Town against 0.50 for the 3DS one)
    ('object_link_child', 'gLinkHumanSkel'): '11:20:0.8,1:-8:0.85:0.68:60:180',
    ('object_link_nuts', 'gLinkDekuSkel'): '1:-8:0.85:0.68:85:180,6:-8:0.85:0.68:85:180',   # the hat and tunic greens, as for human Link (the hair is yellow-olive, hue 60)
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
            rows.append((obj, skel, arch, best[0], len(parent), best[1], ''))
    for (obj, skel), (arch, cm, rig) in sorted(RIG.items()):
        path = Path(a.romfs, 'actors', arch + '.gar.lzs')
        if arch not in cache:
            cache[arch] = binary.gar(path.read_bytes())
        parent = read_parents(z, f'objects/{obj}/{skel}')
        m = cmb.parse(cache[arch][cm], decode_textures=False)
        rows.append((obj, skel, arch, cm, len(parent), len(m['indices']) // 3, rig))
    lines = ['// Generated by tools/newdawn/mm3d/chartable.py: N64 Flex skeleton, 3DS actor archive and CMB with the same limb',
             '// hierarchy, props to leave out (material indices) and whether the model has been reviewed in game.',
             '// {object, skeleton, archive (zelda2_*), cmb, skip materials, reviewed, light reference, saturation, rig pairs, face states, tint}']
    for obj, skel, arch, cm, limbs, tris, rig in rows:
        lr, sat = COLOUR.get((obj, skel), (0, 0))
        lines.append('{ "%s", "%s", "%s", "%s", "%s", %s, %.2ff, %.2ff, "%s", "%s", "%s" },  // %d limbs, %d triangles' % (
            obj, skel, arch, cm, SKIP.get((obj, skel), ''), 'true' if (obj, skel) in REVIEWED else 'false', lr, sat, rig,
            FACE.get((obj, skel), ''), TINT.get((obj, skel), ''), limbs, tris))
    text = '\n'.join(lines) + '\n'
    if a.check:
        print(text)
    else:
        OUT.write_text(text)
    print(f'{len(rows)} skeletons', file=sys.stderr)


if __name__ == '__main__':
    main()
