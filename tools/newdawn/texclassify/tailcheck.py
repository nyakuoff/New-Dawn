"""Spot-check sheets for the unreviewed tail: every tail entry labelled with a material class, grouped by class.
usage: tailcheck.py <work> <final.json> <outdir> [per_sheet]; labels `idx class`; corrections go into tail_decisions.txt as `idx=class`."""
import json, os, pickle, sys
sys.path.insert(0, os.path.dirname(__file__))
import sheets
from review import AB

work, final, outdir = sys.argv[1:4]
per = int(sys.argv[4]) if len(sys.argv) > 4 else 135
F = json.load(open(final))
imgs = pickle.load(open(os.path.join(work, 'tex.pkl'), 'rb'))
items = sorted([n for n, v in F.items() if v['rv'] == 0 and v['c'] not in ('unique', 'skip')], key=lambda n: (F[n]['c'], n))
os.makedirs(outdir, exist_ok=True)
for s in range(0, len(items), per):
    sheets.sheet(items[s:s + per], imgs, '%s/tail_%04d.png' % (outdir, s), cols=15, size=84, start=s,
                 label=lambda i, p: '%d %s' % (i, AB[F[p]['c']]))
json.dump(items, open(os.path.join(work, 'tail_items.json'), 'w'))
print(len(items), 'tail material cells')
