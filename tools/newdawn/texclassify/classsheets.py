"""Final per-class contact sheets (the deliverable review view): one set of sheets per class from final.json, heaviest first.
usage: classsheets.py <work> <final.json> <outdir> [per_class_cap_for_unique_skip]"""
import json, os, pickle, sys
sys.path.insert(0, os.path.dirname(__file__))
import sheets
from review import AB, weight

work, final, outdir = sys.argv[1:4]
cap = int(sys.argv[4]) if len(sys.argv) > 4 else 300
F = json.load(open(final)); feat = json.load(open(os.path.join(work, 'feat.json')))
imgs = pickle.load(open(os.path.join(work, 'tex.pkl'), 'rb'))
os.makedirs(outdir, exist_ok=True)
for c in sorted({v['c'] for v in F.values()}):
    items = sorted([n for n, v in F.items() if v['c'] == c], key=lambda n: -(weight(feat[n]) + 1e-6 * feat[n].get('use_a0', 0)))
    if c in ('unique', 'skip'):
        items = items[:cap]
    for s in range(0, len(items), 150):
        sheets.sheet(items[s:s + 150], imgs, '%s/%s_%02d.png' % (outdir, c, s // 150), cols=15, size=84, start=s,
                     label=lambda i, p: '%d %.1f%s' % (i, F[p]['conf'], '' if F[p]['rv'] else '*'))
    print(c, len(items))
