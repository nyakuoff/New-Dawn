"""N64 -> MM3D REGION registration (milestone 4): where `match3ds.py` names the 3DS texture a wall uses, this registers the N64 texture's UV
rectangle onto that 3DS atlas by geometry, so only the sub-region that shows the same surface is compared and later used as the source.

  match  <out.json>    for every N64 unique scene texture: per-triangle affine N64 texel -> 3DS UV (three vertices of the N64 triangle projected
                       onto the matching 3DS triangle, whose UVs are affine on its plane), clustered by area; the winner is the registration.
                       Each registration is scored by comparing the 3DS crop (resampled to the N64 grid) with the original: colour, luminance
                       correlation at three scales, gradient-magnitude correlation. Writes names and numbers only (no pixels).
  sheet  <cands.json> <outdir> [--min S] [--cols N] [--rows N] [--density D] [--all]
                       review sheets: ID, N64 original | registered 3DS region | whole 3DS texture, and a decisions.txt template (`ID ?`); the sheets
                       are game-derived and stay outside the repository.
  table  <cands.json> <out.json>
                       the committed table (mm3d_regions.json): the registrations of the sheets plus every approved one.
  zoom   <cands.json> <decisions.txt> ID,ID,... <out.png> [size]
                       the same cells at larger size for a second look at chosen IDs.
  apply  <cands.json> <decisions.txt> <recipes.json>
                       writes the approved lines (`ID r|w|y`) into recipes.json unique.mm3d_regions_verified {resource: r|w|y}; everything else
                       stays N64-sourced.
Local use only: it reads the player's own mm.o2r and MM3D RomFS."""
import collections, json, os, pathlib, sys, struct, glob
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'mm3d'))
sys.path.insert(0, HERE)
import o2r, walk, match3ds                     # noqa: E402
import scene, cmb, binary                       # noqa: E402
CELL, MIN_DOT = match3ds.CELL, match3ds.MIN_DOT
DEBUG = os.environ.get('R3_DEBUG', '')

ROMFS = os.path.expanduser(os.environ.get('ND_MM3D_ROMFS', '~/dev/ghm-mods/mm3d/romfs'))
TABLES = os.path.join(HERE, '..', '..', '..', 'mm', 'assets', 'custom', 'newdawn_textures')
FLIP_V = True            # CMB v runs bottom-up (OpenGL); image rows run top-down


def geometry(romfs, name):
    """3DS room triangles with UVs after the material's first UV scale/translate: P (T,3,3), N (T,3), UV (T,3,2), texture name per tri."""
    R = pathlib.Path(romfs) / 'scenes'
    parsed = scene.parse((R / ('%s_info.zsi' % name)).read_bytes())
    rooms = []
    for s in scene.setups(parsed):
        for ref in s['rooms']:
            n = pathlib.Path(ref).name
            if n not in rooms:
                rooms.append(n)
    import hashlib
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
                    if not 0 <= ti < len(m['textures']):
                        continue
                    sx, sy, rot, tx, ty = mat['coordinators'][0]['transform']
                    if abs(rot) > 1e-4:
                        continue                      # rotated coordinate generators are effects, not walls
                    t = ix[d['first_index']:d['first_index'] + d['index_count']].reshape(-1, 3)
                    if len(t) and t.max() >= len(v):
                        continue
                    uv = v[t][:, :, 6:8].astype(np.float64) * [sx, sy] + [tx, ty]
                    P.append(v[t][:, :, :3]); Nn.append(v[t][:, :, 3:6].mean(1)); UV.append(uv); TEX += [m['textures'][ti]['name']] * len(t)
    if not P:
        return None
    return np.concatenate(P).astype(np.float64), np.concatenate(Nn).astype(np.float64), np.concatenate(UV), np.array(TEX)


def scene_textures(romfs, scene_name):
    """name -> RGBA8 (h,w,4) level 0 of every texture in every CMB of the scene's info and room files."""
    out = {}
    for f in sorted(glob.glob(f'{romfs}/scenes/{scene_name}_info.zsi') + glob.glob(f'{romfs}/scenes/{scene_name}_[0-9]*_info.zsi')):
        data = binary.inflate(open(f, 'rb').read())
        i = 0
        while True:
            i = data.find(b'cmb ', i)
            if i < 0:
                break
            size = struct.unpack_from('<I', data, i + 4)[0]
            if 0x80 < size <= len(data) - i:
                try:
                    for t in cmb.parse(bytes(data[i:i + size]))['textures']:
                        out.setdefault(t['name'], np.asarray(t['levels'][0][0]))
                except Exception:
                    pass
            i += 4
    return out


def reduce_tile(uv, size, cm):
    """Texel coordinates of a triangle's vertices inside the tile that holds its centroid: repeat subtracts whole tiles, mirror reflects the
    odd ones, clamp keeps them."""
    out = uv.copy()
    for a in range(2):
        if cm[a] & 2:
            continue
        k = np.floor(uv[:, a].mean() / size[a])
        out[:, a] = ((k + 1) * size[a] - uv[:, a]) if (cm[a] & 1 and int(k) % 2) else uv[:, a] - k * size[a]
    return out


def project_uv(tri, uv, p):
    """3DS UV at the plane point of p (barycentric extrapolation over the triangle's plane)."""
    a, b, c = tri
    e1, e2, d = b - a, c - a, p - a
    g11, g12, g22 = e1 @ e1, e1 @ e2, e2 @ e2
    det = g11 * g22 - g12 * g12
    if det < 1e-9:
        return None
    r1, r2 = d @ e1, d @ e2
    s = (r1 * g22 - r2 * g12) / det
    t = (r2 * g11 - r1 * g12) / det
    return uv[0] + s * (uv[1] - uv[0]) + t * (uv[2] - uv[0])


def sample(img, m, w, h, k, wrap=True):
    """Resample the 3DS image through m (N64 texel (x,y) -> 3DS uv: u = m0 x + m1 y + m2, v = m3 x + m4 y + m5), output (h*k, w*k, 3) float."""
    H, W = img.shape[:2]
    ys, xs = np.mgrid[0:h * k, 0:w * k]
    x, y = (xs + 0.5) / k, (ys + 0.5) / k
    u = m[0] * x + m[1] * y + m[2]
    v = m[3] * x + m[4] * y + m[5]
    px = u * W - 0.5
    py = ((1 - v) if FLIP_V else v) * H - 0.5
    x0, y0 = np.floor(px).astype(int), np.floor(py).astype(int)
    fx, fy = (px - x0)[..., None], (py - y0)[..., None]
    f = img[..., :3].astype(np.float64)

    def at(yy, xx):
        return f[yy % H, xx % W]
    return (at(y0, x0) * (1 - fx) * (1 - fy) + at(y0, x0 + 1) * fx * (1 - fy) + at(y0 + 1, x0) * (1 - fx) * fy + at(y0 + 1, x0 + 1) * fx * fy)


def lum(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def reduce_to(a, n):
    """Area average of a (h,w[,c]) to n x n (block means over the nearest integer-ish grid)."""
    h, w = a.shape[:2]
    ys = np.linspace(0, h, n + 1).astype(int)
    xs = np.linspace(0, w, n + 1).astype(int)
    out = np.zeros((n, n) + a.shape[2:])
    for i in range(n):
        for j in range(n):
            out[i, j] = a[ys[i]:max(ys[i + 1], ys[i] + 1), xs[j]:max(xs[j + 1], xs[j] + 1)].reshape(-1, *a.shape[2:]).mean(0)
    return out


def ncc(a, b):
    a, b = a - a.mean(), b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 1e-9 else 0.0


def grad_mag(a):
    gy, gx = np.gradient(a)
    return np.hypot(gx, gy)


def metrics(orig, crop):
    """orig (h,w,3), crop (h*k, w*k, 3) floats 0..255 -> colour L1 and three correlations (0..1 good)."""
    h, w = orig.shape[:2]
    k = crop.shape[0] // h
    c1 = reduce_to(crop, h) if k > 1 else crop            # crop on the original grid
    lo, lc = lum(orig), lum(c1)
    col = float(np.abs(orig.reshape(-1, 3).mean(0) - c1.reshape(-1, 3).mean(0)).max())
    n = min(h, w)
    s16 = ncc(reduce_to(lo, min(16, n)), reduce_to(lc, min(16, n)))
    s8 = ncc(reduce_to(lo, min(8, n)), reduce_to(lc, min(8, n)))
    s32 = ncc(reduce_to(lo, min(32, n)), reduce_to(lc, min(32, n)))
    ge = ncc(reduce_to(grad_mag(lo), min(32, n)), reduce_to(grad_mag(lc), min(32, n)))
    return dict(col=round(col, 1), n8=round(s8, 3), n16=round(s16, 3), n32=round(s32, 3), edge=round(ge, 3))


def wanted_textures():
    cl = json.load(open(os.path.join(TABLES, 'classification.json')))['textures']
    rec = json.load(open(os.path.join(TABLES, 'recipes.json')))
    cmap, mc = rec.get('class_map', {}), rec.get('min_conf', 0.6)
    out = {}
    for r, e in cl.items():
        c = e.get('class', '')
        if not r.startswith('scenes/') or c in ('skip', 'water', 'foliage') or 'pal_alt' in e:
            continue
        if c in cmap and e.get('conf', 0) >= mc:
            continue
        out[r] = e
    return out


def decode_n64(arc, res, e):
    pal = None
    if 'pal' in e and e['pal'] in arc.names_set:
        pal = arc.tlut(e['pal'])
    try:
        a, grey = arc.decode(res, pal)
    except Exception:
        return None
    return a


def do_match(out_path, only=None):
    wanted = wanted_textures()
    table = {'scenes/nonmq/' + s['segment']: s['mm3d_scene'] for s in json.load(open(os.path.join(HERE, '..', 'mm3d', 'scene-table.json')))}
    arc = o2r.Archive()
    arc.names_set = set(arc.names)
    tris = collections.defaultdict(list)       # texture -> [(p (3,3), uv (3,2), area)]
    W = walk.Walker(arc)

    def sink(owner, t0, t1, p, cr, area, v):
        if t0 and t0 in wanted and area > 1.0:
            tris[t0].append((p, W.uv.copy(), area, W.cm))
    W.sink = sink
    dls = [n for n in arc.names if n.startswith('scenes/') and arc.rtype(n) == 'ODLT']
    called = {arc.hash2name[ex] for n in dls for w0, w1, ex in arc.dl(n) if (w0 >> 24) == 0x31 and ex in arc.hash2name}
    for r in dls:
        if r not in called and (not only or '/%s/' % only in r):
            W.run(r, o2r.scene_of(r))
    print(len(tris), 'wanted textures drawn', file=sys.stderr)
    byscene = collections.defaultdict(list)
    for t in tris:
        byscene[o2r.scene_of(t)].append(t)
    result = {}
    for folder, names in sorted(byscene.items()):
        mname = table.get(folder)
        if not mname or (only and not folder.endswith('/' + only)):
            continue
        try:
            geo = geometry(ROMFS, mname)
            tex3 = scene_textures(ROMFS, mname)
        except Exception as e:
            print('skip', folder, repr(e)[:90], file=sys.stderr)
            continue
        if geo is None:
            continue
        P, Nn, UV, TEX = geo
        nok = 0
        # N64 side of this scene: triangles of the wanted textures, with a grid over their bounding boxes
        F = [(t, q, uv, ar, cm) for t in names for q, uv, ar, cm in tris[t]]
        FP = np.array([f[1] for f in F])                      # (n,3,3)
        FUV = np.array([f[2] for f in F])                     # (n,3,2)
        FT = [f[0] for f in F]
        FCM = [f[4] for f in F]
        e1, e2 = FP[:, 1] - FP[:, 0], FP[:, 2] - FP[:, 0]
        cr = np.cross(e1, e2)
        FN = cr / (np.linalg.norm(cr, axis=1, keepdims=True) + 1e-12)
        g11, g12, g22 = (e1 * e1).sum(1), (e1 * e2).sum(1), (e2 * e2).sum(1)
        gdet = g11 * g22 - g12 * g12
        grid = collections.defaultdict(list)
        big = []
        lo, hi = FP.min(1) - 6.0, FP.max(1) + 6.0
        for i in range(len(F)):
            c0, c1 = np.floor(lo[i] / CELL).astype(int), np.floor(hi[i] / CELL).astype(int)
            if (c1 - c0 + 1).prod() > 64:
                big.append(i)
                continue
            for x in range(c0[0], c1[0] + 1):
                for y in range(c0[1], c1[1] + 1):
                    for z in range(c0[2], c1[2] + 1):
                        grid[(x, y, z)].append(i)
        TA = (P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
        TC = np.cross(TA[0], TA[1])
        TAREA = 0.5 * np.linalg.norm(TC, axis=1)
        TN = TC / (np.linalg.norm(TC, axis=1, keepdims=True) + 1e-12)
        TCEN = P.mean(1)
        votes = collections.defaultdict(collections.Counter)   # N64 texture -> 3DS texture -> area
        regs = collections.defaultdict(lambda: collections.defaultdict(list))
        for m in range(len(P)):
            if not TEX[m] or TAREA[m] < 0.5:
                continue
            cand = grid.get(tuple(np.floor(TCEN[m] / CELL).astype(int)), []) + big
            if not cand:
                continue
            ci = np.array(cand)
            d = np.abs(((TCEN[m] - FP[ci, 0]) * FN[ci]).sum(1))
            ok = (d <= 6.0) & ((FN[ci] @ TN[m]) >= MIN_DOT) & (gdet[ci] > 1e-9)
            if not ok.any():
                continue
            ci, d = ci[ok], d[ok]
            r1 = ((TCEN[m] - FP[ci, 0]) * e1[ci]).sum(1)
            r2 = ((TCEN[m] - FP[ci, 0]) * e2[ci]).sum(1)
            ss = (r1 * g22[ci] - r2 * g12[ci]) / gdet[ci]
            tt = (r2 * g11[ci] - r1 * g12[ci]) / gdet[ci]
            inside = (ss >= -0.05) & (tt >= -0.05) & (ss + tt <= 1.05)
            if not inside.any():
                continue
            ci, d = ci[inside], d[inside]
            f = int(ci[np.argmin(d)])
            t = FT[f]
            votes[t][TEX[m]] += TAREA[m]
            # 3DS vertices on the N64 triangle's plane -> N64 texel coordinates there (exact, the triangle is planar)
            V = P[m] - FP[f, 0]
            q1, q2 = V @ e1[f], V @ e2[f]
            sv = (q1 * g22[f] - q2 * g12[f]) / gdet[f]
            tv = (q2 * g11[f] - q1 * g12[f]) / gdet[f]
            uv64 = FUV[f, 0] + np.outer(sv, FUV[f, 1] - FUV[f, 0]) + np.outer(tv, FUV[f, 2] - FUV[f, 0])      # (3,2) texels
            fmt, tw0, th0, _ = arc.tex_raw(t)
            A = np.column_stack([reduce_tile(uv64, (tw0, th0), FCM[f]), np.ones(3)])
            if abs(np.linalg.det(A)) < 1e-6:
                continue
            regs[t][TEX[m]].append((float(TAREA[m]), np.linalg.solve(A, UV[m]).T))
            if DEBUG and t.endswith(DEBUG) and len(regs[t][TEX[m]]) <= 12:
                Mx = np.linalg.solve(A, UV[m]).T
                print('DBG', TEX[m], 'area %.0f' % TAREA[m], 'M*dims', np.round(Mx[:, :2] * [tw0, th0], 3).ravel().tolist(), 'b', np.round(Mx[:, 2], 2).tolist(),
                      'uv64', np.round(uv64, 1).ravel().tolist(), 'uv3', np.round(UV[m], 2).ravel().tolist(), file=sys.stderr)
        for t in names:
            orig = decode_n64(arc, t, wanted[t])
            if orig is None or (orig[..., 3] < 250).any() or t not in votes:
                continue
            th, tw = orig.shape[:2]
            if min(th, tw) < 8:
                continue
            tot = sum(x[2] for x in tris[t])
            hit = sum(votes[t].values())
            name3, a3 = votes[t].most_common(1)[0]
            img = tex3.get(name3)
            lst = regs[t].get(name3)
            if img is None or not lst:
                continue
            # dominant registration: coarse cells of (linear part per tile, offset), heaviest cell + neighbours, area-weighted mean
            cells = collections.defaultdict(float)
            for a, M in lst:
                cells[(tuple(np.round(M[:, :2].ravel() * [tw, th, tw, th] * 8).astype(int)), tuple(np.round((M[:, 2] % 1.0) * 16).astype(int)))] += a
            (lin, off), _ = max(cells.items(), key=lambda kv: kv[1])
            sel = [(a, M) for a, M in lst if np.abs(np.round(M[:, :2].ravel() * [tw, th, tw, th] * 8) - lin).max() <= 1
                   and np.abs(((M[:, 2] % 1.0) * 16 - np.array(off) + 8) % 16 - 8).max() <= 1.5]
            aw = sum(a for a, M in sel)
            M = sum(a * M for a, M in sel) / aw
            o3 = orig[..., :3].astype(np.float64)

            def evaluate(image, m6, kind, name):
                H, Wd = image.shape[:2]
                # crop density: 3DS pixels per N64 texel along each axis (|column| of the linear part x 3DS size)
                dx = float(np.hypot(m6[0] * Wd, m6[3] * H))
                dy = float(np.hypot(m6[1] * Wd, m6[4] * H))
                sc = int(min(4, max(1, round(min(dx, dy)))))
                met = metrics(o3, sample(image, m6, tw, th, sc))
                return dict(scene=mname, mm3d=name, hyp=kind, m=[round(float(x), 6) for x in m6], size=[tw, th], scale=sc,
                            density=[round(dx, 2), round(dy, 2)], tex3=[Wd, H], **met)
            options = [evaluate(img, [M[0, 0], M[0, 1], M[0, 2], M[1, 0], M[1, 1], M[1, 2]], 'region', name3)]
            # the same art on a differently scaled wall: the whole 3DS texture on the whole N64 texture (identity and flips)
            for nm, _ in votes[t].most_common(2):
                im = tex3.get(nm)
                if im is None or abs((im.shape[1] / im.shape[0]) / (tw / th) - 1) > 0.08:
                    continue
                for fu, fv in ((0, 0), (1, 0), (0, 1), (1, 1)):
                    m6 = [(-1 if fu else 1) / tw, 0, 1 if fu else 0, 0, (1 if fv else -1) / th, 0 if fv else 1]
                    options.append(evaluate(im, m6, 'whole', nm))
            blocks = {'r': options[0]}
            if len(options) > 1:
                blocks['w'] = max(options[1:], key=rank)
            best = max(blocks, key=lambda k: rank(blocks[k]))
            res = dict(scene=mname, size=[tw, th], best=best, share=round(votes[t][blocks[best]['mm3d']] / hit, 3),
                       reg=round(aw / sum(a for a, _ in lst), 3), coverage=round(min(1.0, hit / tot), 3), n=len(tris[t]))
            for k, bl in blocks.items():
                res[k] = {x: y for x, y in bl.items() if x not in ('scene', 'size', 'hyp')}
            result[t] = res
            nok += 1
        print(folder, len(names), 'unique drawn;', nok, 'registered', file=sys.stderr)
    json.dump(result, open(out_path, 'w'), indent=0)
    print(len(result), 'registrations')


def rank(e):
    """One number for sorting the review: structure correlation on three scales and the gradient correlation (of one hypothesis block)."""
    return 0.4 * e['n16'] + 0.2 * e['n8'] + 0.2 * e['n32'] + 0.2 * e['edge']


def top(e):
    return rank(e[e['best']])


def candidates(C, floor, density, skip=frozenset()):
    """Registrations worth a look: structure correlation above `floor`, a 3DS crop denser than the N64 texture, the geometry agrees on the
    texture (share) and covers the surface; best first."""
    return sorted(((r, e) for r, e in C.items() if top(e) >= floor and max(min(b['density']) for k, b in e.items() if k in ('r', 'w')) >= density
                   and e['share'] >= 0.5 and e['coverage'] >= 0.3 and r not in skip), key=lambda kv: -top(kv[1]))


def do_table(cands, out, floor, density):
    """The committed table (names and numbers): the registrations of the review sheets, in the shape RuntimeTextures reads."""
    C = json.load(open(cands))
    rec = json.load(open(os.path.join(TABLES, 'recipes.json')))['unique']
    keep = {r: e for r, e in candidates(C, floor, density)}
    keep.update({r: C[r] for r in rec.get('mm3d_regions_verified', {}) if r in C})
    for e in keep.values():
        e.pop('region_rank', None)
    json.dump({'version': 1, 'comment': 'tools/newdawn/texclassify/region3ds.py: N64 unique scene texture -> registration on the MM3D atlas (r: sub-region affine, '
               'w: whole texture), names and numbers only. See docs/TEXTURE_PIPELINE.md.', 'textures': dict(sorted(keep.items()))},
              open(out, 'w'), separators=(',', ':'))
    print(len(keep), 'entries ->', out)


def do_sheet(cands, outdir, floor, cols, rows, density, include_verified):
    from PIL import Image, ImageDraw
    C = json.load(open(cands))
    rec = json.load(open(os.path.join(TABLES, 'recipes.json')))['unique']
    have = set(rec.get('mm3d_verified', [])) | set(rec.get('mm3d_regions_verified', {}))
    items = candidates(C, floor, density, have if not include_verified else set())
    os.makedirs(outdir, exist_ok=True)
    arc = o2r.Archive()
    arc.names_set = set(arc.names)
    cl = json.load(open(os.path.join(TABLES, 'classification.json')))['textures']
    cache = {}
    S = 150
    cw, ch = 3 * S + 16, S + 32
    per = cols * rows
    lines = ['# One line per candidate: replace ? with  r  (use the registered region crop, middle panel),  w  (use the whole 3DS texture, right panel)',
             '# or n. Lines left as ? stay N64-sourced. Panels: N64 original | region crop | whole 3DS texture.', '# ID  decision  rank  resource']
    for pg in range(0, len(items), per):
        sh = Image.new('RGB', (cols * cw, rows * ch), (24, 24, 24))
        d = ImageDraw.Draw(sh)
        for i, (res, e) in enumerate(items[pg:pg + per]):
            cid = 'R%04d' % (pg + i + 1)
            if e['scene'] not in cache:
                cache[e['scene']] = scene_textures(ROMFS, e['scene'])
            orig = decode_n64(arc, res, cl[res])
            w, h = e['size']
            x, y = (i % cols) * cw, (i // cols) * ch
            d.text((x + 3, y + 2), '%s %s %s%s' % (cid, res.split('/')[2][:18], 'r%.2f' % top(e), ' *' if res in have else ''), fill=(255, 255, 160))
            panels = [orig[..., :3]]
            txt = []
            for k in ('r', 'w'):
                b = e.get(k)
                img = cache[e['scene']].get(b['mm3d']) if b else None
                if img is None:
                    panels.append(None)
                    continue
                panels.append(sample(img, b['m'], w, h, b['scale']).clip(0, 255).astype(np.uint8))
                txt.append('%s %.2f %dx' % (k, rank(b), b['scale']))
            d.text((x + 3, y + 14), ' | '.join(txt) + ('  best ' + e['best']), fill=(170, 170, 170))
            for k, im in enumerate(panels):
                if im is None:
                    continue
                a = Image.fromarray(im)
                f = S / max(a.size)
                sh.paste(a.resize((max(1, int(a.width * f)), max(1, int(a.height * f))), Image.BICUBIC), (x + 4 + k * (S + 4), y + 28))
            lines.append('%s ?  %.3f  %s' % (cid, top(e), res))
        sh.save(os.path.join(outdir, 'sheet_%02d.png' % (pg // per + 1)))
    open(os.path.join(outdir, 'decisions.txt'), 'w').write('\n'.join(lines) + '\n')
    print(len(items), 'candidates on', (len(items) + per - 1) // per, 'sheets ->', outdir)


def do_zoom(cands, decisions, ids, out, size, cols=2):
    """Larger panels for chosen IDs of an existing decisions file (the IDs of the sheets stay valid): `zoom <cands.json> <decisions.txt> R0004,R0009 <out.png> [size]`."""
    from PIL import Image, ImageDraw
    C = json.load(open(cands))
    byid = {}
    for l in open(decisions):
        f = l.split()
        if f and not l.startswith('#') and len(f) >= 4:
            byid[f[0]] = f[3]
    arc = o2r.Archive()
    arc.names_set = set(arc.names)
    cl = json.load(open(os.path.join(TABLES, 'classification.json')))['textures']
    cache = {}
    S = size
    cw, ch = 3 * S + 16, S + 32
    rows = (len(ids) + cols - 1) // cols
    sh = Image.new('RGB', (cols * cw, rows * ch), (24, 24, 24))
    d = ImageDraw.Draw(sh)
    for i, cid in enumerate(ids):
        res = byid[cid]
        e = C[res]
        if e['scene'] not in cache:
            cache[e['scene']] = scene_textures(ROMFS, e['scene'])
        orig = decode_n64(arc, res, cl[res])
        w, h = e['size']
        x, y = (i % cols) * cw, (i // cols) * ch
        d.text((x + 3, y + 2), '%s %s r%.2f' % (cid, res.split('/')[2][:22], top(e)), fill=(255, 255, 160))
        panels, txt = [orig[..., :3]], []
        for k in ('r', 'w'):
            b = e.get(k)
            img = cache[e['scene']].get(b['mm3d']) if b else None
            if img is None:
                panels.append(None)
                continue
            panels.append(sample(img, b['m'], w, h, b['scale']).clip(0, 255).astype(np.uint8))
            txt.append('%s %.2f %dx' % (k, rank(b), b['scale']))
        d.text((x + 3, y + 14), ' | '.join(txt) + '  best ' + e['best'], fill=(170, 170, 170))
        for k, im in enumerate(panels):
            if im is None:
                continue
            a = Image.fromarray(im)
            f = S / max(a.size)
            sh.paste(a.resize((max(1, int(a.width * f)), max(1, int(a.height * f))), Image.BICUBIC), (x + 4 + k * (S + 4), y + 28))
    sh.save(out)


def do_apply(decisions, recipes_path):
    """`ID r|w|y|n resource`: r = region crop, w = whole texture, y = the better of the two by rank (resolved when the table is read), n = reject (and withdraw an earlier approval)."""
    yes, no = {}, set()
    for l in open(decisions):
        f = l.split()
        if f and not l.startswith('#') and len(f) >= 4:
            if f[1].lower() in ('r', 'w', 'y'):
                yes[f[3]] = f[1].lower()
            elif f[1].lower() == 'n':
                no.add(f[3])
    rec = json.load(open(recipes_path))
    cur = rec['unique'].get('mm3d_regions_verified', {})
    cur.update(yes)
    for r in no:                      # an explicit n withdraws an earlier approval
        cur.pop(r, None)
    rec['unique']['mm3d_regions_verified'] = dict(sorted(cur.items()))
    open(recipes_path, 'w').write(json.dumps(rec, indent=1, ensure_ascii=False))
    print(len(yes), 'approved;', len(cur), 'in total')


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'match':
        do_match(sys.argv[2], sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None)
    elif cmd == 'sheet':
        a = sys.argv

        def opt(n, d):
            return type(d)(a[a.index(n) + 1]) if n in a else d
        do_sheet(a[2], a[3], opt('--min', 0.15), opt('--cols', 3), opt('--rows', 6), opt('--density', 1.4), '--all' in a)
    elif cmd == 'table':
        do_table(sys.argv[2], sys.argv[3], 0.15, 1.4)
    elif cmd == 'zoom':
        do_zoom(sys.argv[2], sys.argv[3], sys.argv[4].split(','), sys.argv[5], int(sys.argv[6]) if len(sys.argv) > 6 else 220)
    elif cmd == 'apply':
        do_apply(sys.argv[3], sys.argv[4])
    else:
        print(__doc__)
