"""Label the tail (textures outside the visually reviewed head) from their nearest reviewed neighbours in CLIP space, combined with
the automatic classifier.  Conservative: a material label is kept only when kNN and the automatic label agree, or the neighbours are
unanimous and very close; every other tail entry becomes unique (or skip when both say skip).  Tail entries get rv = 0.
usage: propagate.py <work> <review_labels.json> <out final.json>"""
import json, os, sys, collections
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from prompts import CLASSES
MATERIAL = [c for c in CLASSES if c not in ('unique', 'skip')]


def main(work, review, out):
    feat = json.load(open(os.path.join(work, 'feat.json')))
    names = json.load(open(os.path.join(work, 'clip_names.json')))
    es = np.load(os.path.join(work, 'clip_single.npy')); et = np.load(os.path.join(work, 'clip_tile.npy'))
    X = np.concatenate([et, es], 1)
    X /= np.linalg.norm(X, axis=1, keepdims=True)
    auto = json.load(open(os.path.join(work, 'auto.json')))
    rev = json.load(open(review))
    pos = {n: i for i, n in enumerate(names)}
    R = [n for n in rev]
    XR = X[[pos[n] for n in R]]
    final = {}
    for n in rev:
        final[n] = dict(c=rev[n], conf=1.0, rv=1, why='review')
    tail = [n for n in names if n not in rev]
    S = X[[pos[n] for n in tail]] @ XR.T
    stats = collections.Counter()
    for j, n in enumerate(tail):
        o = np.argsort(-S[j])[:7]
        w = np.exp((S[j][o] - S[j][o[0]]) * 25)
        votes = collections.defaultdict(float)
        for k, ww in zip(o, w):
            votes[rev[R[k]]] += ww
        tot = sum(votes.values())
        best = max(votes, key=votes.get)
        share = votes[best] / tot
        sim = float(S[j][o[0]])
        a = auto[n]['c']
        f = feat[n]
        if best in MATERIAL:
            if a == best or (share >= 0.9 and sim >= 0.93):
                c, conf = best, min(0.85, 0.5 + 0.4 * share * sim)
            elif a in ('skip',) and best == 'skip':
                c, conf = 'skip', 0.6
            else:
                c, conf = 'unique', 0.5      # disagreement: do not replace
        else:
            c, conf = best, min(0.85, 0.4 + 0.5 * share * sim)
        # hard evidence overrides
        if f.get('am_xlu', 0) >= 0.8 and c in MATERIAL and c != 'water':
            c, conf = 'skip', 0.55
        final[n] = dict(c=c, conf=round(float(conf), 3), rv=0, why='knn%.2f/%.2f' % (share, sim))
        stats[c] += 1
    json.dump(final, open(out, 'w'))
    print('tail', len(tail), sorted(stats.items(), key=lambda x: -x[1]))


if __name__ == '__main__':
    main(*sys.argv[1:4])
