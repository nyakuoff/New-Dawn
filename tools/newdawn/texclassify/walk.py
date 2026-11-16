"""Static F3DEX2 display-list interpreter over the player's own mm.o2r.

For every root display list (one no other display list calls) it follows G_DL calls with inherited RDP state and records
every triangle batch as a "draw": which texture sits in tile 0 / tile 1, which TLUT, the combiner, tile wrap state, the
alpha mode, and geometry statistics (world area, texels per world unit).  Output: JSON lines of per-(owner, draw-state)
aggregates, written outside the repository (game-derived: texture paths with statistics).  Nothing here renders anything.
"""
import json, struct, sys, os, math, collections, argparse
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import o2r

OP_VTX, OP_TRI1, OP_TRI2, OP_QUAD = 0x01, 0x05, 0x06, 0x07
CC_A = ['COMBINED', 'TEXEL0', 'TEXEL1', 'PRIM', 'SHADE', 'ENV', '1', 'NOISE'] + ['0'] * 8
CC_B = ['COMBINED', 'TEXEL0', 'TEXEL1', 'PRIM', 'SHADE', 'ENV', 'KEYCENTER', 'K4'] + ['0'] * 8
CC_C = ['COMBINED', 'TEXEL0', 'TEXEL1', 'PRIM', 'SHADE', 'ENV', 'KEYSCALE', 'COMBINED_A', 'TEXEL0_A', 'TEXEL1_A',
        'PRIM_A', 'SHADE_A', 'ENV_A', 'LODFRAC', 'PRIMLODFRAC', 'K5'] + ['0'] * 16
CC_D = ['COMBINED', 'TEXEL0', 'TEXEL1', 'PRIM', 'SHADE', 'ENV', '1', '0']
AC_ABD = ['COMBINED', 'TEXEL0', 'TEXEL1', 'PRIM', 'SHADE', 'ENV', '1', '0']
AC_C = ['LODFRAC', 'TEXEL0', 'TEXEL1', 'PRIM', 'SHADE', 'ENV', 'PRIMLODFRAC', '0']


def decode_combine(w0, w1):
    a0 = (w0 >> 20) & 15; c0 = (w0 >> 15) & 31; Aa0 = (w0 >> 12) & 7; Ac0 = (w0 >> 9) & 7
    a1 = (w0 >> 5) & 15; c1 = w0 & 31
    b0 = (w1 >> 28) & 15; b1 = (w1 >> 24) & 15; d0 = (w1 >> 15) & 7
    Ab0 = (w1 >> 12) & 7; Ad0 = (w1 >> 9) & 7
    Aa1 = (w1 >> 21) & 7; Ac1 = (w1 >> 18) & 7; d1 = (w1 >> 6) & 7; Ab1 = (w1 >> 3) & 7; Ad1 = w1 & 7
    rgb0 = (CC_A[a0], CC_B[b0], CC_C[c0], CC_D[d0]); rgb1 = (CC_A[a1], CC_B[b1], CC_C[c1], CC_D[d1])
    al0 = (AC_ABD[Aa0], AC_ABD[Ab0], AC_C[Ac0], AC_ABD[Ad0]); al1 = (AC_ABD[Aa1], AC_ABD[Ab1], AC_C[Ac1], AC_ABD[Ad1])
    return rgb0, al0, rgb1, al1


def fmt_eq(t):
    a, b, c, d = t
    return '(%s-%s)*%s+%s' % t


class State:
    __slots__ = ('timg', 'tiles', 'tmem', 'tlut', 'texon', 'tex_tile', 'tex_s', 'tex_t', 'cc', 'omh', 'oml', 'geom',
                 'vc', 'prim_lod')

    def __init__(self):
        self.timg = None
        self.tiles = [dict(fmt=0, siz=0, tmem=0, pal=0, cmS=0, cmT=0, maskS=0, maskT=0, shS=0, shT=0, w=0, h=0)
                      for _ in range(8)]
        self.tmem = {}      # tmem word -> texture path
        self.tlut = {}      # tmem word -> tlut path
        self.texon = 0
        self.tex_tile = 0
        self.tex_s = self.tex_t = 0xFFFF
        self.cc = None
        self.omh = 0
        self.oml = 0
        self.geom = 0
        self.vc = [None] * 64
        self.prim_lod = 0

    def clone(self):
        n = State()
        n.timg = self.timg
        n.tiles = [dict(t) for t in self.tiles]
        n.tmem = dict(self.tmem)
        n.tlut = dict(self.tlut)
        n.texon, n.tex_tile, n.tex_s, n.tex_t = self.texon, self.tex_tile, self.tex_s, self.tex_t
        n.cc, n.omh, n.oml, n.geom = self.cc, self.omh, self.oml, self.geom
        n.vc = list(self.vc)
        return n


def shift_factor(sh):
    """RDP tile coordinate shift: 0 none, 1..10 shift right (divide by 2^n), 11..15 shift left (multiply by 2^(16-n))."""
    return 1.0 if sh == 0 else (2.0 ** -sh if sh <= 10 else 2.0 ** (16 - sh))


def alpha_mode(oml):
    zmode = (oml >> 10) & 3
    cvg_x_alpha = (oml >> 12) & 1
    alpha_cvg_sel = (oml >> 13) & 1
    force_bl = (oml >> 14) & 1
    ac = oml & 3
    if zmode == 2:
        return 'xlu'
    if ac == 1 or (cvg_x_alpha and alpha_cvg_sel):
        return 'cutout'
    return 'opa'


class Walker:
    def __init__(self, arc):
        self.arc = arc
        self.draws = collections.defaultdict(lambda: dict(area=0.0, tris=0, du=[], dv=[], wt=[]))
        self.called = set()
        self._fmt = {}
        self.sink = None   # optional callback(owner, tex0, tex1_or_None, vertices(3,3), area, uv(3,2)) for every triangle
        self.budget = 0

    def run(self, root, owner):
        self.owner = owner
        self.budget = 200000
        self.exec(root, State(), 0)

    def exec(self, name, st, depth):
        if depth > 12:
            return
        arc = self.arc
        cmds = arc.dl(name)
        for w0, w1, ex in cmds:
            self.budget -= 1
            if self.budget < 0:
                return
            op = w0 >> 24
            if op == 0xDF:
                return
            if op == 0x31:  # G_DL_OTR_HASH: call (push) or branch; RDP state persists across the call
                nm = arc.hash2name.get(ex)
                if nm:
                    self.exec(nm, st, depth + 1)
                if (w0 >> 16) & 1:
                    return
                continue
            if op == 0x32:  # VTX
                nm = arc.hash2name.get(ex)
                n = (w0 >> 12) & 0xFF
                v0 = ((w0 >> 1) & 0x7F) - n
                if nm and 0 <= v0 and v0 + n <= 64:
                    arr = arc.vtx(nm)
                    off = w1 // 16
                    for i in range(n):
                        st.vc[v0 + i] = arr[off + i] if off + i < len(arr) else None
                continue
            if op == 0xFD:  # unexpanded SETTIMG (rare)
                continue
            if op == 0x20:  # SETTIMG_OTR_HASH
                nm = arc.hash2name.get(ex)
                st.timg = nm
                continue
            if op == 0xFC:
                st.cc = decode_combine(w0, w1)
            elif op == 0xF5:  # SETTILE
                t = st.tiles[(w1 >> 24) & 7]
                t['fmt'] = (w0 >> 21) & 7; t['siz'] = (w0 >> 19) & 3; t['tmem'] = w0 & 0x1FF
                t['pal'] = (w1 >> 20) & 15
                t['cmT'] = (w1 >> 18) & 3; t['maskT'] = (w1 >> 14) & 15; t['shT'] = (w1 >> 10) & 15
                t['cmS'] = (w1 >> 8) & 3; t['maskS'] = (w1 >> 4) & 15; t['shS'] = w1 & 15
            elif op == 0xF2:  # SETTILESIZE
                t = st.tiles[(w1 >> 24) & 7]
                t['w'] = (((w1 >> 12) & 0xFFF) - ((w0 >> 12) & 0xFFF)) // 4 + 1
                t['h'] = ((w1 & 0xFFF) - (w0 & 0xFFF)) // 4 + 1
            elif op in (0xF3, 0xF4):  # LOADBLOCK / LOADTILE
                t = st.tiles[(w1 >> 24) & 7]
                if st.timg:
                    st.tmem[t['tmem']] = st.timg
            elif op == 0xF0:  # LOADTLUT
                t = st.tiles[(w1 >> 24) & 7]
                if st.timg:
                    st.tlut[t['tmem']] = st.timg
            elif op == 0xD7:  # TEXTURE
                st.tex_tile = (w0 >> 8) & 7
                st.texon = (w0 >> 1) & 0x7F
                st.tex_s = (w1 >> 16) & 0xFFFF
                st.tex_t = w1 & 0xFFFF
            elif op == 0xE3 or op == 0xE2:  # SETOTHERMODE_H / L
                ln = (w0 & 0xFF) + 1
                sft = 32 - ((w0 >> 8) & 0xFF) - ln
                mask = ((1 << ln) - 1) << sft
                if op == 0xE3:
                    st.omh = (st.omh & ~mask) | (w1 & mask)
                else:
                    st.oml = (st.oml & ~mask) | (w1 & mask)
            elif op == 0xD9:  # GEOMETRYMODE (clear, set)
                st.geom = (st.geom & (w0 & 0xFFFFFF)) | w1
            elif op == 0xFA:  # SETPRIMCOLOR
                st.prim_lod = (w0 >> 8) & 0xFF
            elif op == OP_TRI1:
                self.tri(st, ((w0 >> 16) & 0xFF) >> 1, ((w0 >> 8) & 0xFF) >> 1, (w0 & 0xFF) >> 1)
            elif op == OP_TRI2:
                self.tri(st, ((w0 >> 16) & 0xFF) >> 1, ((w0 >> 8) & 0xFF) >> 1, (w0 & 0xFF) >> 1)
                self.tri(st, ((w1 >> 16) & 0xFF) >> 1, ((w1 >> 8) & 0xFF) >> 1, (w1 & 0xFF) >> 1)
            elif op == OP_QUAD:
                a, b, c = ((w0 >> 16) & 0xFF) >> 1, ((w0 >> 8) & 0xFF) >> 1, (w0 & 0xFF) >> 1
                d = ((w1 >> 16) & 0xFF) >> 1
                e = ((w1 >> 8) & 0xFF) >> 1
                f = (w1 & 0xFF) >> 1
                self.tri(st, a, b, c)
                self.tri(st, d, e, f)

    def fmt_of(self, tex):
        f = self._fmt.get(tex)
        if f is None:
            f = self._fmt[tex] = self.arc.tex_header(tex)[0] if tex in self.arc.names_set else -1
        return f

    # tile (render) -> (texture path, tlut path)
    def bound(self, st, ti):
        t = st.tiles[ti]
        tex = st.tmem.get(t['tmem'])
        tl = None
        if tex is not None:
            fmt = self.fmt_of(tex)
            if fmt in (3, 4):
                tl = st.tlut.get(256 + t['pal'] * 16) or st.tlut.get(256) or (list(st.tlut.values())[-1] if st.tlut else None)
        return tex, tl

    def tri(self, st, i0, i1, i2):
        if not st.texon or st.cc is None:
            return
        v = (st.vc[i0], st.vc[i1], st.vc[i2])
        if v[0] is None or v[1] is None or v[2] is None:
            return
        p = np.array([[float(x['x']), float(x['y']), float(x['z'])] for x in v])
        e1, e2 = p[1] - p[0], p[2] - p[0]
        cr = np.cross(e1, e2)
        area = 0.5 * math.sqrt(float(cr @ cr))
        t0, tl0 = self.bound(st, st.tex_tile)
        t1, tl1 = self.bound(st, (st.tex_tile + 1) & 7)
        rgb0, al0, rgb1, al1 = st.cc
        use = ' '.join((rgb0 + al0 + rgb1 + al1))
        uses1 = 'TEXEL1' in use or 'TEXEL1_A' in use
        key = (self.owner, t0, tl0, t1 if uses1 else None, tl1 if uses1 else None, st.cc, alpha_mode(st.oml),
               (st.geom >> 18) & 1, st.tiles[st.tex_tile]['cmS'], st.tiles[st.tex_tile]['cmT'],
               st.tiles[st.tex_tile]['maskS'], st.tiles[st.tex_tile]['maskT'],
               st.tiles[st.tex_tile]['shS'], st.tiles[st.tex_tile]['shT'],
               st.tiles[(st.tex_tile + 1) & 7]['shS'], st.tiles[(st.tex_tile + 1) & 7]['shT'])
        if self.sink is not None:
            self.sink(self.owner, t0, t1 if uses1 else None, p, cr, area, v)
        d = self.draws[key]
        d['area'] += area
        d['tris'] += 1
        if area > 1e-3 and t0:
            # texel density: gradients of (s,t) in texels over the triangle plane
            sc_s = st.tex_s / 65536.0
            sc_t = st.tex_t / 65536.0
            uv = np.array([[float(x['s']) / 32.0 * sc_s, float(x['t']) / 32.0 * sc_t] for x in v])
            # shift (G_TX_NOLOD etc.) ignored; tiles with shiftS/T are rare in world meshes
            f1, f2 = uv[1] - uv[0], uv[2] - uv[0]
            n = cr / math.sqrt(float(cr @ cr))
            b1 = e1 / np.linalg.norm(e1) if np.linalg.norm(e1) > 0 else None
            if b1 is not None:
                b2 = np.cross(n, b1)
                E = np.array([[e1 @ b1, e1 @ b2], [e2 @ b1, e2 @ b2]])
                if abs(np.linalg.det(E)) > 1e-6:
                    G = np.linalg.solve(E, np.stack([f1, f2]))  # G[:,0]=grad u, G[:,1]=grad v  (2x2: rows=plane axes)
                    gu = float(np.hypot(G[0, 0], G[1, 0]))
                    gv = float(np.hypot(G[0, 1], G[1, 1]))
                    if gu < 50 and gv < 50:  # texels per world unit
                        d['du'].append(gu); d['dv'].append(gv); d['wt'].append(area)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--o2r', default=o2r.DEFAULT_O2R)
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', help='substring filter on the root DL path')
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    arc = o2r.Archive(a.o2r)
    arc.names_set = set(arc.names)
    dls = [n for n in arc.names if (n.startswith('scenes/') or n.startswith('objects/') or n.startswith('misc/'))
           and arc.rtype(n) == 'ODLT']
    print(len(dls), 'display lists', file=sys.stderr)
    # call graph: which DLs are called by other DLs
    called = set()
    for n in dls:
        for w0, w1, ex in arc.dl(n):
            if (w0 >> 24) == 0x31 and ex in arc.hash2name:
                called.add(arc.hash2name[ex])
    roots = [n for n in dls if n not in called]
    print(len(roots), 'roots', file=sys.stderr)
    if a.only:
        roots = [n for n in roots if a.only in n]
    if a.limit:
        roots = roots[:a.limit]
    W = Walker(arc)
    for i, r in enumerate(roots):
        W.run(r, o2r.scene_of(r))
        if i % 500 == 0:
            print(i, len(roots), len(W.draws), file=sys.stderr)
    out = []
    for k, d in W.draws.items():
        owner, t0, tl0, t1, tl1, cc, am, texgen, cmS, cmT, mS, mT, sS0, sT0, sS1, sT1 = k
        row = dict(owner=owner, t0=t0, tl0=tl0, t1=t1, tl1=tl1, cc=[list(x) for x in cc], am=am, texgen=texgen,
                   wrap=[cmS, cmT, mS, mT], sh=[sS0, sT0, sS1, sT1], area=round(d['area'], 1), tris=d['tris'])
        if d['wt']:
            w = np.array(d['wt']); du = np.array(d['du']); dv = np.array(d['dv'])
            o = np.argsort(du); c = np.cumsum(w[o]); row['du'] = round(float(du[o][np.searchsorted(c, c[-1] / 2)]), 4)
            o = np.argsort(dv); c = np.cumsum(w[o]); row['dv'] = round(float(dv[o][np.searchsorted(c, c[-1] / 2)]), 4)
        out.append(row)
    with open(a.out, 'w') as f:
        for r in out:
            f.write(json.dumps(r) + '\n')
    print(len(out), 'draw states ->', a.out, file=sys.stderr)


if __name__ == '__main__':
    main()
