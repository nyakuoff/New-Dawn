"""Second review pass over the textures that dominate the *measured* screen (top N per measured scene), at a larger size, with the current label.
usage: pass2.py <work> <final.json> <outdir> [N] ; corrections `idx=class` -> pass2_decisions.txt, merged by finalize.py as reviewed."""
import json, os, pickle, sys
sys.path.insert(0, os.path.dirname(__file__))
import sheets
from review import AB

work, final, outdir = sys.argv[1:4]
N = int(sys.argv[4]) if len(sys.argv) > 4 else 36
F = json.load(open(final)); M = json.load(open(os.path.join(work, 'measured.json')))
imgs = pickle.load(open(os.path.join(work, 'tex.pkl'), 'rb'))
items, share, seen = [], {}, set()
for scene, m in M.items():
    k = 0
    for p, s in m['share'].items():
        if p not in F or p in seen:
            continue
        seen.add(p); items.append(p); share[p] = (m['name'][:6], s); k += 1
        if k >= N:
            break
os.makedirs(outdir, exist_ok=True)
per = 120
for s in range(0, len(items), per):
    sheets.sheet(items[s:s + per], imgs, '%s/pass2_%04d.png' % (outdir, s), cols=12, size=104, start=s,
                 label=lambda i, p: '%d %s %.1f%%' % (i, AB[F[p]['c']], share[p][1] * 100))
json.dump(items, open(os.path.join(work, 'pass2_items.json'), 'w'))
print(len(items), 'cells')
