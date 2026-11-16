"""Merge: review labels (head) + propagated tail labels + hand corrections of the tail (tail_decisions.txt over tail_items.json)
and of the second pass over the measured-screen top textures (pass2_decisions.txt over pass2_items.json).
Writes <work>/final.json  {path: {c, conf, rv, why}}.  usage: finalize.py <work> <final0.json> <tail_decisions.txt> [pass2_decisions.txt] [late_corrections.txt]"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from prompts import CLASSES

work, final0, dec = sys.argv[1:4]
F = json.load(open(final0))
items = json.load(open(os.path.join(work, 'tail_items.json')))
n = 0
for line in open(dec):
    for tok in line.split('#')[0].split():
        i, c = tok.split('=')
        assert c in CLASSES, tok
        p = items[int(i)]
        F[p].update(c=c, conf=0.8, rv=1, why='tail-review')
        n += 1
if len(sys.argv) > 4:
    items2 = json.load(open(os.path.join(work, 'pass2_items.json')))
    for line in open(sys.argv[4]):
        for tok in line.split('#')[0].split():
            i, c = tok.split('=')
            assert c in CLASSES, tok
            p = items2[int(i)]
            F[p].update(c=c, conf=1.0, rv=1, why='pass2')
            n += 1
if len(sys.argv) > 5:
    for line in open(sys.argv[5]):
        line = line.split('#')[0].strip()
        if line:
            p, c = line.rsplit('=', 1)
            assert c in CLASSES and p in F, line
            F[p].update(c=c, conf=1.0, rv=1, why='late')
            n += 1
# tail material entries that were looked at on the spot-check sheets and kept are reviewed too
for p in items:
    if F[p]['rv'] == 0:
        F[p].update(rv=1, conf=max(F[p]['conf'], 0.75), why=F[p]['why'] + '+spot')
json.dump(F, open(os.path.join(work, 'final.json'), 'w'))
import collections
print(n, 'tail corrections;', sorted(collections.Counter(v['c'] for v in F.values()).items(), key=lambda x: -x[1]))
