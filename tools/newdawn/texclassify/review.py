"""Review sheets ordered by visible weight, each cell labelled `index class conf`.  Corrections are written by hand as
`index=class` lines into a text file and merged by `apply_review.py`.
usage: review.py <work> <outdir> <group: scenes|objects|misc|all> <start> <count> [per_sheet] [--xlu] [--unused]"""
import json, os, pickle, sys
sys.path.insert(0, os.path.dirname(__file__))
import sheets

AB = dict(brick='brk', cobble='cob', flagstone='flg', wood_planks='pln', wood_beam='bem', plaster='pls', roof_tiles='rof',
          thatch='tch', dirt='drt', sand='snd', mud='mud', rock='rck', grass='grs', moss='mos', metal='mtl', fabric='fab',
          rope='rop', water='wat', ice_snow='ice', foliage='fol', unique='UNQ', skip='skp')


def weight(f):
    a0, a1 = f.get('use_a0', 0), f.get('use_a1', 0)
    return a0 * (f.get('am_opa', 0) + f.get('am_cutout', 0)) + a1 * 0.5


def order(feat, group, kind):
    names = [n for n in feat if group == 'all' or n.startswith(group + '/')]
    if kind == 'opa':
        names = [n for n in names if weight(feat[n]) > 0]
        names.sort(key=lambda n: -weight(feat[n]))
    elif kind == 'xlu':
        names = [n for n in names if weight(feat[n]) == 0 and feat[n].get('use_a0', 0) > 0]
        names.sort(key=lambda n: -feat[n].get('use_a0', 0))
    else:   # unused
        names = [n for n in names if feat[n].get('use_a0', 0) + feat[n].get('use_a1', 0) == 0]
        names.sort()
    return names


if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    kind = 'xlu' if '--xlu' in sys.argv else 'unused' if '--unused' in sys.argv else 'opa'
    work, outdir, group, start, count = a[0], a[1], a[2], int(a[3]), int(a[4])
    per = int(a[5]) if len(a) > 5 else 120
    auto = os.path.join(work, 'labels.json') if os.path.exists(os.path.join(work, 'labels.json')) else os.path.join(work, 'auto.json')
    A = json.load(open(auto))
    feat = json.load(open(os.path.join(work, 'feat.json')))
    imgs = pickle.load(open(os.path.join(work, 'tex.pkl'), 'rb'))
    names = order(feat, group, kind)
    CLS = 'all'
    for x in sys.argv:
        if x.startswith('--cls='):
            CLS = x[6:]
            names = [n for n in names if A[n]['c'] == CLS]
    sel = names[start:start + count]
    os.makedirs(outdir, exist_ok=True)
    for s in range(0, len(sel), per):
        sheets.sheet(sel[s:s + per], imgs, '%s/%s_%s_%s_%05d.png' % (outdir, group, kind, CLS, start + s), cols=15, size=84,
                     start=start + s, label=lambda i, p: '%d %s %.1f' % (i, AB[A[p]['c']], A[p]['conf']))
    print(group, kind, 'total', len(names), 'sheets of', len(sel))
