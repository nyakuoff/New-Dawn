"""Cluster the review head (textures that carry 99 % of visible surface, plus all xlu-only scene textures) by CLIP embedding
and write review sheets: cells ordered by cluster then distance to the cluster centre, labelled `index cluster auto-class`.
Decisions are typed by hand as `cN=class` (whole cluster) and `idx=class` (single cell); apply_review.py turns them into overrides.
usage: cluster.py <work> <outdir> [k]"""
import json, os, pickle, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import sheets
from review import AB, weight


def head_set(feat):
    out = []
    for grp, q in (('scenes', 0.995), ('objects', 0.99), ('misc', 1.0)):
        names = [n for n in feat if n.startswith(grp + '/')]
        names.sort(key=lambda n: -weight(feat[n]))
        w = np.array([weight(feat[n]) for n in names])
        c = np.cumsum(w) / w.sum()
        k = int(np.searchsorted(c, q)) + 1
        out += names[:k]
        # translucent-only textures are part of the review too (water, fog, effects)
        xl = [n for n in names[k:] if feat[n].get('use_a0', 0) > 0 and weight(feat[n]) == 0]
        xl.sort(key=lambda n: -feat[n]['use_a0'])
        out += xl[:150 if grp == 'objects' else 400]
    return list(dict.fromkeys(out))


def kmeans(X, k, seed=1, iters=40):
    rng = np.random.default_rng(seed)
    C = [X[rng.integers(len(X))]]
    d = ((X - C[0]) ** 2).sum(1)
    for _ in range(k - 1):
        C.append(X[rng.choice(len(X), p=d / d.sum())])
        d = np.minimum(d, ((X - C[-1]) ** 2).sum(1))
    C = np.array(C)
    for _ in range(iters):
        a = np.argmin(((X[:, None, :] - C[None]) ** 2).sum(2) if len(X) * k < 4e6 else
                      np.stack([((X - c) ** 2).sum(1) for c in C], 1), 1)
        for j in range(k):
            m = X[a == j]
            if len(m):
                C[j] = m.mean(0)
    return a, C


if __name__ == '__main__':
    work, outdir = sys.argv[1], sys.argv[2]
    k = int(sys.argv[3]) if len(sys.argv) > 3 else 110
    feat = json.load(open(os.path.join(work, 'feat.json')))
    names = json.load(open(os.path.join(work, 'clip_names.json')))
    es = np.load(os.path.join(work, 'clip_single.npy')); et = np.load(os.path.join(work, 'clip_tile.npy'))
    pos = {n: i for i, n in enumerate(names)}
    A = json.load(open(os.path.join(work, 'auto.json')))
    head = head_set(feat)
    X = np.concatenate([et, es], 1)[[pos[n] for n in head]]
    X = X / np.linalg.norm(X, axis=1, keepdims=True)
    a, C = kmeans(X, k)
    dist = ((X - C[a]) ** 2).sum(1)
    # order clusters by dominant auto class then by size so neighbouring sheets look alike
    cl_class = {}
    for j in range(k):
        mem = [head[i] for i in np.where(a == j)[0]]
        if not mem:
            continue
        cl_class[j] = max(set(A[m]['c'] for m in mem), key=lambda c: sum(A[m]['c'] == c for m in mem))
    cls_order = sorted(cl_class, key=lambda j: (cl_class[j], -int((a == j).sum())))
    seq = []
    for j in cls_order:
        idx = np.where(a == j)[0]
        idx = idx[np.argsort(dist[idx])]
        seq += [(head[i], j) for i in idx]
    imgs = pickle.load(open(os.path.join(work, 'tex.pkl'), 'rb'))
    os.makedirs(outdir, exist_ok=True)
    per = 150
    for s in range(0, len(seq), per):
        chunk = seq[s:s + per]
        cmap = {p: j for p, j in chunk}
        sheets.sheet([p for p, _ in chunk], imgs, '%s/head_%04d.png' % (outdir, s), cols=15, size=84, start=s,
                     label=lambda i, p: '%d c%d %s' % (i, cmap[p], AB[A[p]['c']]))
    json.dump([[p, int(j)] for p, j in seq], open(os.path.join(work, 'head_order.json'), 'w'))
    print(len(seq), 'head textures,', len(cls_order), 'clusters,', (len(seq) + per - 1) // per, 'sheets')
