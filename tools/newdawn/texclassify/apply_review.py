"""Turn the hand-typed review decisions (cluster defaults `cN=class|@family`, single cells `idx=class`) into path -> class labels.
Families keep the automatic label when it belongs to the family, otherwise use the fallback:
  @earth {dirt sand mud rock grass moss} -> dirt     @stone {brick cobble flagstone rock} -> flagstone
usage: apply_review.py <work> <decisions.txt> <out review_labels.json>"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from prompts import CLASSES

FAM = {'@earth': ({'dirt', 'sand', 'mud', 'rock', 'grass', 'moss'}, 'dirt'),
       '@stone': ({'brick', 'cobble', 'flagstone', 'rock'}, 'flagstone')}


def parse(text):
    cdef, exc = {}, {}
    for line in text.splitlines():
        line = line.split('#')[0]
        for tok in line.split():
            k, v = tok.split('=')
            if v not in CLASSES and v not in FAM:
                raise SystemExit('bad class %r in %r' % (v, tok))
            if k.startswith('c'):
                cdef[int(k[1:])] = v
            else:
                exc[int(k)] = v
    return cdef, exc


def apply(work, decisions):
    order = json.load(open(os.path.join(work, 'head_order.json')))
    auto = json.load(open(os.path.join(work, 'auto.json')))
    cdef, exc = parse(open(decisions).read())
    out, missing = {}, 0
    for i, (p, c) in enumerate(order):
        v = exc.get(i, cdef.get(c))
        if v is None:
            missing += 1
            continue
        if v in FAM:
            fam, fb = FAM[v]
            v = auto[p]['c'] if auto[p]['c'] in fam else fb
        out[p] = v
    return out, missing


if __name__ == '__main__':
    out, missing = apply(sys.argv[1], sys.argv[2])
    json.dump(out, open(sys.argv[3], 'w'), indent=0, sort_keys=True)
    import collections
    print(len(out), 'reviewed,', missing, 'cells without a decision', sorted(collections.Counter(out.values()).items(), key=lambda x: -x[1]))
