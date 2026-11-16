"""Decode every world/character texture of the player's mm.o2r with its palette and compute numeric features.

Inputs : mm.o2r, the draw table written by walk.py (for palettes and usage).
Outputs (outside the repository, game-derived): <work>/tex.pkl  {path: RGBA uint8 array}
                                                <work>/feat.json {path: {feature: number}}
"""
import argparse, collections, json, os, pickle, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import o2r


def usage_table(walk_path):
    """Per texture: area by role/alpha/wrap, palettes by area, scenes."""
    U = collections.defaultdict(lambda: dict(a0=0.0, a1=0.0, am=collections.Counter(), clampS=0.0, clampT=0.0,
                                             mirror=0.0, wrap=0.0, texgen=0.0, pal=collections.Counter(),
                                             owners=collections.Counter(), n=0))
    for l in open(walk_path):
        r = json.loads(l)
        for k, pk, role in (('t0', 'tl0', 'a0'), ('t1', 'tl1', 'a1')):
            t = r[k]
            if not t:
                continue
            u = U[t]
            ar = r['area'] + 1e-3
            u[role] += ar
            u['am'][r['am']] += ar
            cmS, cmT = r['wrap'][0], r['wrap'][1]
            if k == 't0':
                u['clampS'] += ar * (cmS & 2 > 0)
                u['clampT'] += ar * (cmT & 2 > 0)
                u['mirror'] += ar * ((cmS & 1) or (cmT & 1))
                u['wrap'] += ar * (cmS == 0 and cmT == 0)
            u['texgen'] += ar * (r['texgen'] > 0)
            if r[pk]:
                u['pal'][r[pk]] += ar
            u['owners'][r['owner']] += ar
            u['n'] += 1
    return U


def hsv(a):
    rgb = a[..., :3].astype(np.float32) / 255
    mx = rgb.max(-1); mn = rgb.min(-1); d = mx - mn
    h = np.zeros_like(mx)
    m = d > 1e-6
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    h = np.where(m & (mx == r), ((g - b) / np.where(m, d, 1)) % 6, h)
    h = np.where(m & (mx == g), (b - r) / np.where(m, d, 1) + 2, h)
    h = np.where(m & (mx == b), (r - g) / np.where(m, d, 1) + 4, h)
    return h * 60.0, np.where(mx > 0, d / np.where(mx > 0, mx, 1), 0), mx


def peak(ac, lo=3):
    """strongest autocorrelation peak at lag >= lo (1-D, already normalised to ac[0] = 1)."""
    n = len(ac) // 2
    if n <= lo + 1:
        return 0, 0.0
    seg = ac[lo:n + 1]
    i = int(np.argmax(seg))
    return lo + i, float(seg[i])


def features(a):
    h, w = a.shape[:2]
    rgb = a[..., :3].astype(np.float32) / 255
    al = a[..., 3].astype(np.float32) / 255
    L = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    H, S, V = hsv(a)
    f = dict(w=w, h=h)
    f['alpha_lo'] = float((al < 0.5).mean())
    f['alpha_mid'] = float(((al > 0.05) & (al < 0.95)).mean())
    f['lum'] = float(L.mean()); f['lum_std'] = float(L.std())
    f['sat'] = float(S.mean()); f['val'] = float(V.mean())
    big = S > 0.15
    for nm, lo, hi in (('red', 340, 25), ('brown', 15, 50), ('yellow', 50, 75), ('green', 75, 170), ('cyan', 170, 200),
                       ('blue', 200, 260), ('purple', 260, 340)):
        m = ((H >= lo) | (H < hi)) if lo > hi else ((H >= lo) & (H < hi))
        f['hue_' + nm] = float((m & big).mean())
    f['grey'] = float((S < 0.15).mean())
    q = (a[..., :3] >> 3).reshape(-1, 3).astype(np.uint32)
    codes = (q[:, 0] << 10) | (q[:, 1] << 5) | q[:, 2]
    u, c = np.unique(codes, return_counts=True)
    p = c / c.sum()
    f['ncol'] = int(len(u)); f['col_entropy'] = float(-(p * np.log2(p)).sum())
    gx = L - np.roll(L, 1, 1); gy = L - np.roll(L, 1, 0)
    ex, ey = float(np.abs(gx).mean()), float(np.abs(gy).mean())
    f['edge'] = ex + ey
    f['aniso'] = (ex - ey) / (ex + ey + 1e-6)     # >0: variation along x (vertical stripes); <0: horizontal stripes/planks
    inner = (np.abs(gx[:, 1:]).mean() + np.abs(gy[1:, :]).mean()) / 2
    seam = (np.abs(L[:, 0] - L[:, -1]).mean() + np.abs(L[0, :] - L[-1, :]).mean()) / 2
    f['seam'] = float(seam / (inner + 1e-4))
    # border vs centre (emblems have a plain border)
    k = max(2, min(h, w) // 6)
    cen = L[k:-k, k:-k] if h > 2 * k and w > 2 * k else L
    bord = np.concatenate([L[:k].ravel(), L[-k:].ravel(), L[:, :k].ravel(), L[:, -k:].ravel()])
    f['border_std'] = float(bord.std()); f['centre_std'] = float(cen.std())
    f['lr_sym'] = float(np.corrcoef(L.ravel(), L[:, ::-1].ravel())[0, 1]) if L.std() > 1e-4 else 0.0
    # autocorrelation periods of the high-passed luminance
    Lh = L - L.mean()
    F = np.fft.rfft2(Lh)
    ac = np.fft.irfft2(np.abs(F) ** 2, s=Lh.shape)
    ac = ac / (ac[0, 0] + 1e-9)
    px, sx = peak(ac[0, :]) if w >= 8 else (0, 0.0)
    py, sy = peak(ac[:, 0]) if h >= 8 else (0, 0.0)
    f['per_x'] = int(px); f['per_xs'] = sx; f['per_y'] = int(py); f['per_ys'] = sy
    # high-frequency share
    P = np.abs(F) ** 2
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.rfftfreq(w)[None, :]
    r = np.hypot(fx, fy)
    tot = P.sum() - P[0, 0] + 1e-9
    f['hf'] = float(P[r > 0.25].sum() / tot)
    # solid / gradient detector
    f['range'] = float(np.percentile(L, 98) - np.percentile(L, 2))
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--o2r', default=o2r.DEFAULT_O2R)
    ap.add_argument('--walk', required=True)
    ap.add_argument('--work', required=True)
    a = ap.parse_args()
    arc = o2r.Archive(a.o2r)
    names = [n for n in arc.names if n.startswith(('scenes/', 'objects/', 'misc/')) and arc.rtype(n) == 'OTEX']
    tl = {n for n in names if 'TLUT' in n}
    tex = sorted(n for n in names if n not in tl and not n.startswith('misc/') or n.startswith('misc/scene_texture'))
    tex = [n for n in tex if n not in tl]
    print(len(tex), 'textures,', len(tl), 'TLUT', file=sys.stderr)
    U = usage_table(a.walk)
    imgs, feats = {}, {}
    nopal = 0
    for i, n in enumerate(tex):
        fmt = arc.tex_header(n)[0]
        pal = None
        u = U.get(n)
        pal_path = None
        if fmt in (3, 4):
            if u and u['pal']:
                pal_path = u['pal'].most_common(1)[0][0]
                pal = arc.tlut(pal_path)
        img, grey = arc.decode(n, pal)
        imgs[n] = img
        f = features(img)
        f['fmt'] = fmt
        f['nopal'] = int(grey)
        nopal += int(grey)
        if pal_path:
            f['pal'] = pal_path
            if len(u['pal']) > 1:
                f['pal_alt'] = [p for p, _ in u['pal'].most_common()[1:4]]
        if u:
            tot = u['a0'] + u['a1'] + 1e-9
            f['use_a0'] = u['a0']; f['use_a1'] = u['a1']
            for k in ('opa', 'xlu', 'cutout'):
                f['am_' + k] = u['am'][k] / tot
            a0 = u['a0'] + 1e-9
            f['clamp'] = max(u['clampS'], u['clampT']) / a0
            f['clamp2'] = min(u['clampS'], u['clampT']) / a0
            f['mirror'] = u['mirror'] / a0
            f['wrapfrac'] = u['wrap'] / a0
            f['texgen'] = u['texgen'] / tot
            f['owners'] = [o for o, _ in u['owners'].most_common(3)]
        feats[n] = f
        if i % 1000 == 0:
            print(i, len(tex), file=sys.stderr)
    pickle.dump(imgs, open(os.path.join(a.work, 'tex.pkl'), 'wb'), protocol=4)
    json.dump(feats, open(os.path.join(a.work, 'feat.json'), 'w'))
    print('done; CI without palette (grey fallback):', nopal, file=sys.stderr)


if __name__ == '__main__':
    main()
