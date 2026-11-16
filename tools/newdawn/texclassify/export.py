"""Build the committed tables (names and numbers only) in mm/assets/custom/newdawn_textures/ from the local work files:
final.json (labels), feat.json (palettes, image stats), walk_all.jsonl (draw states), measured.json (screen shares).
usage: export.py <work> <outdir>"""
import collections, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from prompts import CLASSES
from walk import shift_factor

MPU = 0.025
MATERIAL = set(CLASSES) - {'unique', 'skip'}
SCENE_TABLE = os.path.join(os.path.dirname(__file__), '..', 'mm3d', 'scene-table.json')


def g4(x):
    return float('%.4g' % x)


def wmedian(vals, w):
    o = np.argsort(vals)
    v = np.array(vals)[o]
    c = np.cumsum(np.array(w)[o])
    return float(v[min(len(v) - 1, np.searchsorted(c, c[-1] / 2))])


def pattern(cc):
    r0, a0, r1, a1 = cc
    s = ' '.join(r0 + a0)
    if r0[0] == 'TEXEL1' and r0[1] == '0' and r0[2] == 'TEXEL0' and r0[3] == '0':
        return 'modulate'
    if 'PRIMLODFRAC' in r0[2] and r0[0] == 'TEXEL1' and r0[1] == 'TEXEL0':
        return 'lerp_prim'
    if r0[2] == 'LODFRAC' and r0[0] == 'TEXEL1' and r0[1] == 'TEXEL0':
        return 'lerp_lod'
    if r0[0].startswith('TEXEL') and r0[3].startswith('TEXEL') and r0[2] in ('ENV_A', 'TEXEL0_A', 'TEXEL1_A', 'PRIM_A', 'SHADE_A'):
        return 'lerp_alpha'
    return 'other'


def roles(pat, a, b, cls, feat):
    """role of tile 0 (a) and tile 1 (b)."""
    if a == b:
        return ['base', 'lod_dup']
    ca, cb = cls[a]['c'], cls[b]['c']
    if pat == 'lerp_lod':
        return ['base', 'lod_dup']
    if pat == 'lerp_prim':
        return ['base', 'alt']
    if pat == 'lerp_alpha':
        return ['base', 'decal']
    if ca == 'skip' and cb != 'skip':
        return ['detail', 'base']
    if cb == 'skip':
        return ['base', 'detail']
    if ca == 'unique' and cb in MATERIAL:
        return ['decal', 'base']
    if cb == 'unique' and ca in MATERIAL:
        return ['base', 'decal']
    # modulate / other: the colour carrier (more saturated, smoother) is the base, the grey structured one the detail
    fa, fb = feat[a], feat[b]
    sa = fa['sat'] - 0.5 * fa['edge']
    sb = fb['sat'] - 0.5 * fb['edge']
    return ['base', 'detail'] if sa >= sb else ['detail', 'base']


def main(work, outdir):
    cls = json.load(open(os.path.join(work, 'final.json')))
    feat = json.load(open(os.path.join(work, 'feat.json')))
    rows = [json.loads(l) for l in open(os.path.join(work, 'walk_all.jsonl'))]
    measured = json.load(open(os.path.join(work, 'measured.json')))
    os.makedirs(outdir, exist_ok=True)

    # ---- classification.json --------------------------------------------------
    T = {}
    for n in sorted(cls):
        v, f = cls[n], feat[n]
        e = dict(c=v['c'], conf=round(v['conf'], 2), rv=v['rv'])
        if 'pal' in f:
            e['pal'] = f['pal']
            if f.get('pal_alt'):
                e['pal_alt'] = f['pal_alt']
        T[n] = e
    # rename the keys to the schema names
    T = {n: {('class' if k == 'c' else k): x for k, x in e.items()} for n, e in T.items()}
    dump(outdir, 'classification.json', dict(version=1, classes=CLASSES, textures=T))

    # ---- scale.json -------------------------------------------------------------
    use = collections.defaultdict(list)    # texture -> [(du, dv, area, owner)]
    for r in rows:
        if 'du' not in r:
            continue
        sS0, sT0, sS1, sT1 = r['sh']
        if r['t0']:
            use[r['t0']].append((r['du'] * shift_factor(sS0), r['dv'] * shift_factor(sT0), r['area'], r['owner']))
        if r['t1'] and r['t1'] != r['t0']:
            use[r['t1']].append((r['du'] * shift_factor(sS1), r['dv'] * shift_factor(sT1), r['area'], r['owner']))
    S = {}
    for n, L in use.items():
        if n not in cls:
            continue
        w = [x[2] for x in L]
        du = wmedian([x[0] for x in L], w)
        dv = wmedian([x[1] for x in L], w)
        if du <= 0 or dv <= 0:
            continue
        w_, h_ = feat[n]['w'], feat[n]['h']
        ar = collections.Counter()
        for x in L:
            ar['scene' if x[3].startswith('scenes/') or x[3].startswith('misc/scene_texture') else 'object'] += x[2]
        tu = [round(w_ / du, 1), round(h_ / dv, 1)]
        S[n] = dict(tpu=[g4(du), g4(dv)], tile_units=tu, tile_m=[round(tu[0] * MPU, 2), round(tu[1] * MPU, 2)],
                    space=ar.most_common(1)[0][0], area=round(sum(w)), n=len(L))
    dump(outdir, 'scale.json', dict(version=1, metres_per_unit=MPU, textures=dict(sorted(S.items()))))

    # ---- layers.json ----------------------------------------------------------------
    P = {}
    for r in rows:
        if not r['t1'] or r['t1'] == r['t0']:
            continue
        cc = r['cc']
        pat = pattern(cc)
        k = (r['t0'], r['t1'], pat)
        d = P.setdefault(k, dict(area=0.0, scenes=collections.Counter(), am=collections.Counter(), du=[], dv=[], du1=[], dv1=[],
                                 w=[], cc=None, ccarea=0))
        d['area'] += r['area']
        d['scenes'][r['owner']] += r['area']
        d['am'][r['am']] += r['area']
        if 'du' in r:
            sS0, sT0, sS1, sT1 = r['sh']
            d['du'].append(r['du'] * shift_factor(sS0)); d['dv'].append(r['dv'] * shift_factor(sT0))
            d['du1'].append(r['du'] * shift_factor(sS1)); d['dv1'].append(r['dv'] * shift_factor(sT1)); d['w'].append(r['area'])
        if r['area'] > d['ccarea']:
            d['cc'], d['ccarea'] = cc, r['area']
    pairs = []
    for (a, b, pat), d in P.items():
        if a not in cls or b not in cls:
            continue
        e = dict(a=a, b=b, pattern=pat, roles=roles(pat, a, b, cls, feat))
        if d['w']:
            e['tpu_a'] = [g4(wmedian(d['du'], d['w'])), g4(wmedian(d['dv'], d['w']))]
            e['tpu_b'] = [g4(wmedian(d['du1'], d['w'])), g4(wmedian(d['dv1'], d['w']))]
        e['scenes'] = [s for s, _ in d['scenes'].most_common(6)]
        e['area'] = round(d['area'])
        e['alpha'] = d['am'].most_common(1)[0][0]
        e['cc'] = ['(%s-%s)*%s+%s' % tuple(d['cc'][0]), '(%s-%s)*%s+%s' % tuple(d['cc'][1]),
                   '(%s-%s)*%s+%s' % tuple(d['cc'][2]), '(%s-%s)*%s+%s' % tuple(d['cc'][3])]
        pairs.append(e)
    pairs.sort(key=lambda e: -e['area'])
    dump(outdir, 'layers.json', dict(version=1, roles=['base', 'detail', 'decal', 'alt', 'lod_dup'], patterns=PATTERNS,
                                     pairs=pairs))

    # ---- scene_stats.json ----------------------------------------------------------------
    names = {}
    for s in json.load(open(SCENE_TABLE)):
        names['scenes/nonmq/' + s['segment']] = s['name']
    base_of = {}
    for e in pairs:   # primary texture of a pair draw = its base role
        base_of[(e['a'], e['b'])] = e['a'] if e['roles'][0] in ('base', 'decal', 'alt') else e['b']
    area = collections.defaultdict(lambda: collections.Counter())
    xlu = collections.defaultdict(float); tot = collections.defaultdict(float)
    for r in rows:
        o = r['owner']
        if not o.startswith('scenes/') or not r['t0']:
            continue
        t = base_of.get((r['t0'], r['t1']), r['t0']) if r['t1'] else r['t0']
        if t not in cls:
            continue
        tot[o] += r['area']
        if r['am'] == 'xlu':
            xlu[o] += r['area']
            continue
        c = cls[t]['c']
        # a low-confidence material label is not replaceable
        g = 'replaceable' if (c in MATERIAL and cls[t]['conf'] >= 0.6) else ('skip' if c == 'skip' else 'unique')
        area[o][g] += r['area']
    SC = {}
    for o in sorted(area):
        a = area[o]
        s = sum(a.values())
        if s <= 0:
            continue
        e = dict(name=names.get(o, o.split('/')[-1]),
                 area={g: round(a[g] / s, 3) for g in ('replaceable', 'unique', 'skip')},
                 xlu_area_fraction=round(xlu[o] / tot[o], 3) if tot[o] else 0)
        textures = [n for n in cls if n.startswith(o + '/')]
        e['n_tex'] = len(textures)
        e['n_replaceable'] = sum(1 for n in textures if cls[n]['c'] in MATERIAL and cls[n]['conf'] >= 0.6)
        e['n_unique'] = sum(1 for n in textures if cls[n]['c'] == 'unique' or (cls[n]['c'] in MATERIAL and cls[n]['conf'] < 0.6))
        e['n_skip'] = sum(1 for n in textures if cls[n]['c'] == 'skip')
        if o in measured:
            m = measured[o]['share']
            g = collections.Counter()
            for p, sh in m.items():
                if p in cls:
                    c = cls[p]['c']
                    g['replaceable' if (c in MATERIAL and cls[p]['conf'] >= 0.6) else ('skip' if c == 'skip' else 'unique')] += sh
            ts = sum(g.values())
            e['screen'] = dict({k: round(g[k] / ts, 3) for k in ('replaceable', 'unique', 'skip')}, measured=True,
                               capture=measured[o]['name'], views=measured[o]['captures'])
        SC[o] = e
    dump(outdir, 'scene_stats.json', dict(version=1, scenes=SC))
    # ---- mm3d_match.json ----------------------------------------------------------------
    mpath = os.path.join(work, 'm3.json')
    if os.path.exists(mpath):
        m3 = json.load(open(mpath))
        uniq = [p for p, v in cls.items() if p.startswith('scenes/') and (v['c'] == 'unique' or (v['c'] in MATERIAL and v['conf'] < 0.6))]
        area = collections.Counter()
        for r in rows:
            if r['t0'] and r['am'] != 'xlu':
                area[r['t0']] += r['area']
        scr = collections.Counter()
        for o, mm in measured.items():
            for p, sh in mm['share'].items():
                scr[p] += sh / len(measured)

        def confident(v):
            return v['share'] >= 0.6 and v['coverage'] >= 0.3 and v['margin'] >= 0.2
        M3 = {}
        for p in uniq:
            v = m3.get(p)
            if not v or v['share'] < 0.4 or v['coverage'] < 0.1:
                continue
            e = dict(scene=v['scene'], mm3d=v['mm3d'], score=v['score'], margin=v['margin'], share=v['share'], coverage=v['coverage'])
            if not confident(v):
                e['weak'] = True
            M3[p] = e
        ta = sum(area[p] for p in uniq) or 1.0
        ts = sum(scr[p] for p in uniq) or 1.0
        conf = [p for p in M3 if not M3[p].get('weak')]
        summary = dict(unique_scene_textures=len(uniq), with_candidate=len(M3), confident=len(conf),
                       by_count=dict(candidate=round(len(M3) / len(uniq), 3), confident=round(len(conf) / len(uniq), 3)),
                       by_static_area=dict(candidate=round(sum(area[p] for p in M3) / ta, 3), confident=round(sum(area[p] for p in conf) / ta, 3)),
                       by_measured_screen=dict(candidate=round(sum(scr[p] for p in M3) / ts, 3), confident=round(sum(scr[p] for p in conf) / ts, 3)),
                       confident_rule='share >= 0.6, coverage >= 0.3, margin >= 0.2')
        dump(outdir, 'mm3d_match.json', dict(version=1, coverage=summary, textures=dict(sorted(M3.items()))))
        print('mm3d', summary)
    print(len(T), 'textures;', len(S), 'with scale;', len(pairs), 'layer pairs;', len(SC), 'scenes')


PATTERNS = {
    'modulate': 'TEXEL0 * TEXEL1: both multiply the surface colour (structure or mottling over a colour carrier)',
    'lerp_prim': 'TEXEL0 blended toward TEXEL1 by the primitive LOD fraction: two alternate variants of one surface (base and alt)',
    'lerp_lod': 'TEXEL0 blended toward TEXEL1 by the LOD fraction (mip emulation): treat as the single texture a',
    'lerp_alpha': 'TEXEL0 blended toward TEXEL1 by an alpha (decal over a base)',
    'other': 'anything else (see cc)'}


def dump(outdir, name, obj):
    with open(os.path.join(outdir, name), 'w') as f:
        json.dump(obj, f, separators=(',', ':'), sort_keys=False)
        f.write('\n')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
