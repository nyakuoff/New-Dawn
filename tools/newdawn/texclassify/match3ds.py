"""N64 -> MM3D texture correspondence for `unique` scene textures, by GEOMETRY first (the 3DS rooms are redrawn, so pixels do not match):
the same wall occupies the same place in both games, so for every N64 triangle drawn with a unique texture we find the 3DS triangle at
that position (same coordinate system and scale, normal within ~37 degrees) and vote for the 3DS texture that triangle uses.
  share    = area share of the winning 3DS texture among the triangles of the N64 texture that found any counterpart
  coverage = fraction of the N64 texture's surface area that found a counterpart
  score    = share * min(1, coverage / 0.5);   margin = share - runner-up share
Local use only: it reads the player's own mm.o2r and MM3D RomFS and writes names and numbers (3DS texture name and scores).
usage: match3ds.py <work> <final.json> <romfs> <out.json> [--only SEGMENT]"""
import collections, hashlib, json, os, pathlib, sys
import numpy as np
HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, '..', 'mm3d'))
sys.path.insert(0, HERE)
import o2r, walk                      # noqa: E402
import scene, cmb                     # noqa: E402  (clean-room tools of this repository)

CELL = 32.0
REACH = 16.0
MIN_DOT = 0.8


def mm3d_triangles(romfs, name):
    """(centroid samples (n,3), normals (n,3), tri ids, texture name per tri, uv (T,3,2)) over every room mesh and setup of one MM3D scene."""
    R = pathlib.Path(romfs) / 'scenes'
    zsi = R / ('%s_info.zsi' % name)
    parsed = scene.parse(zsi.read_bytes())
    rooms = []
    for s in scene.setups(parsed):
        for ref in s['rooms']:
            n = pathlib.Path(ref).name
            if n not in rooms:
                rooms.append(n)
    seen, P, Nn, UV, TEX = set(), [], [], [], []
    for rn in rooms:
        room = scene.parse((R / rn).read_bytes())
        for s in scene.setups(room):
            for raw in s['meshes']:
                k = hashlib.sha1(raw['data']).hexdigest()
                if k in seen:
                    continue
                seen.add(k)
                m = cmb.parse(raw['data'])
                v, ix = m['vertices'], m['indices']
                for d in m['draws']:
                    mat = m['materials'][d['material']]
                    ti = mat['bindings'][0]['texture']
                    nm = m['textures'][ti]['name'] if 0 <= ti < len(m['textures']) else ''
                    t = ix[d['first_index']:d['first_index'] + d['index_count']].reshape(-1, 3)
                    if len(t) and t.max() >= len(v):
                        continue
                    P.append(v[t][:, :, :3]); Nn.append(v[t][:, :, 3:6].mean(1)); UV.append(v[t][:, :, 6:8]); TEX += [nm] * len(t)
    if not P:
        return None
    return np.concatenate(P).astype(np.float64), np.concatenate(Nn).astype(np.float64), np.concatenate(UV), np.array(TEX)


def build_grid(P):
    g = collections.defaultdict(list)
    w = np.array([[1 / 3, 1 / 3, 1 / 3], [2 / 3, 1 / 6, 1 / 6], [1 / 6, 2 / 3, 1 / 6], [1 / 6, 1 / 6, 2 / 3]])
    S = np.einsum('sk,tkc->tsc', w, P)            # (T, 4, 3)
    cells = np.floor(S / CELL).astype(np.int64)
    for t in range(len(P)):
        for s in range(4):
            g[tuple(cells[t, s])].append(t)
    return g, S


def nearest(g, S, Nn, c, n):
    cc = np.floor(c / CELL).astype(np.int64)
    best, bt = REACH * REACH, -1
    seen = set()
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for t in g.get((cc[0] + dx, cc[1] + dy, cc[2] + dz), ()):
                    if t in seen:
                        continue
                    seen.add(t)
                    nt = Nn[t]
                    ln = np.linalg.norm(nt)
                    if ln < 1e-6 or abs(float(n @ nt) / ln) < MIN_DOT:
                        continue
                    d2 = float(((S[t] - c) ** 2).sum(1).min())
                    if d2 < best:
                        best, bt = d2, t
    return bt


def main():
    work, final, romfs, out = sys.argv[1:5]
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    F = json.load(open(final))
    material = set(json.load(open(os.path.join(HERE, 'classes.json')))['material'])
    wanted = {p for p, v in F.items() if (p.startswith('scenes/') and (v['c'] == 'unique' or (v['c'] in material and v['conf'] < 0.6)))}
    table = {'scenes/nonmq/' + s['segment']: s['mm3d_scene'] for s in json.load(open(os.path.join(HERE, '..', 'mm3d', 'scene-table.json')))}
    arc = o2r.Archive()
    arc.names_set = set(arc.names)
    # N64 side: triangles of the wanted textures
    tris = collections.defaultdict(list)       # texture -> [(centroid, normal, area)]

    def sink(owner, t0, t1, p, cr, area, v):
        for t in (t0, t1):
            if t and t in wanted and area > 1.0:
                c = p.mean(0)
                n = cr / (np.linalg.norm(cr) + 1e-9)
                tris[t].append((c, n, area))
    W = walk.Walker(arc)
    W.sink = sink
    dls = [n for n in arc.names if n.startswith('scenes/') and arc.rtype(n) == 'ODLT']
    called = {arc.hash2name[ex] for n in dls for w0, w1, ex in arc.dl(n) if (w0 >> 24) == 0x31 and ex in arc.hash2name}
    for r in dls:
        if r not in called and (not only or '/%s/' % only in r):
            W.run(r, o2r.scene_of(r))
    print(len(tris), 'wanted textures drawn with geometry', file=sys.stderr)
    result, per_scene = {}, {}
    byscene = collections.defaultdict(list)
    for t in tris:
        byscene[o2r.scene_of(t)].append(t)
    for folder, names in sorted(byscene.items()):
        mname = table.get(folder)
        if not mname or (only and not folder.endswith('/' + only)):
            continue
        try:
            geo = mm3d_triangles(romfs, mname)
        except Exception as e:       # scene absent from MM3D or an unreadable file
            print('skip', folder, repr(e)[:90], file=sys.stderr)
            continue
        if geo is None:
            continue
        P, Nn, UV, TEX = geo
        g, S = build_grid(P)
        for t in names:
            votes, tot, hit = collections.Counter(), 0.0, 0.0
            for c, n, a in tris[t]:
                tot += a
                ti = nearest(g, S, Nn, c, n)
                if ti < 0 or not TEX[ti]:
                    continue
                hit += a
                votes[TEX[ti]] += a
            if not votes or tot <= 0:
                continue
            (name, va), *rest = votes.most_common(2) + [(None, 0)] if len(votes) < 2 else votes.most_common(2)
            share = va / hit
            second = (rest[0][1] / hit) if rest and rest[0][0] else 0.0
            cov = hit / tot
            result[t] = dict(scene=mname, mm3d=name, src='room', score=round(share * min(1.0, cov / 0.5), 3),
                             margin=round(share - second, 3), share=round(share, 3), coverage=round(cov, 3),
                             n=len(tris[t]))
        per_scene[folder] = len(names)
        print(folder, len(names), 'unique drawn;', sum(1 for t in names if t in result), 'with a counterpart', file=sys.stderr)
    json.dump(result, open(out, 'w'))
    print(len(result), 'matches')


if __name__ == '__main__':
    main()
