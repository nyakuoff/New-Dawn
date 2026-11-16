#!/usr/bin/env python3
"""Reference-vs-converted gate for the 3DS character models (docs/MM3D_CHARACTERS.md, "Reference gate").

REFERENCE: the CMB drawn the way the 3DS would: its own materials (PICA texture environment combiners, material
ambient/diffuse/emission constants, fragment lighting, vertex colours), in its bind pose, under neutral studio lighting.
Nothing from New Dawn's pipeline: no N64 grade, no post.

CONVERTED: the models the in-game converter wrote (mm3d-characters.o2r): the limb display lists, vertex arrays and
RGBA32 textures, drawn the way the N64 microcode would (texel x (ambient + diffuse x N.L) x primitive colour) under the same
neutral studio lights, in the same bind pose (each limb's matrix is its 3DS bone's bind matrix, undoing the rest shift).

Output per model: a side-by-side sheet (reference | converted | difference) for front, side and back, and numbers:
per-texture mean colour in both and their CIE76 delta E, silhouette IoU and the extents of the head, torso and leg bands.
The gate fails (exit 1) when any texture's delta E or a silhouette extent exceeds the tolerances.

  refgate.py --romfs RomFS --o2r mm.o2r --cache mm3d-characters.o2r --out DIR [--object object_link_child --skeleton gLinkHumanSkel]

Clean-room: CMB parsing is the repository's cmb.py; PICA semantics come from the format facts in docs/MM3D_CHARACTERS.md.
Never commit the generated images or anything derived from the dump.
"""
import argparse
import re
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import binary  # noqa: E402
import cmb  # noqa: E402

HERE = Path(__file__).resolve()
TABLE = HERE.parents[3] / 'mm/2s2h/NewDawn/Mm3dCharacterTable.inc'

# ------------------------------------------------------------------ GL / PICA enums
SRC_TEX = {0x84C0: 0, 0x84C1: 1, 0x84C2: 2, 0x84C3: 3}
SRC_CONSTANT, SRC_PRIMARY, SRC_PREVIOUS, SRC_PREV_BUFFER = 0x8576, 0x8577, 0x8578, 0x8579
SRC_FRAG_PRIMARY, SRC_FRAG_SECONDARY = 0x6210, 0x6211


def crc64(name):
    v, poly, mask = (1 << 64) - 1, 0x42f0e1eba9ea3693, (1 << 64) - 1
    for b in name.encode():
        v ^= b << 56
        for _ in range(8):
            v = ((v << 1) ^ poly if v & (1 << 63) else v << 1) & mask
    return v


# ------------------------------------------------------------------ table rows
def read_table(path=TABLE):
    rows = []
    rx = re.compile(r'\{ "(\w+)", "(\w+)", "(\w+)", "([^"]*)", "([^"]*)", (true|false), ([\d.]+)f, ([\d.]+)f, "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)" \}')
    for line in open(path):
        m = rx.match(line)
        if m:
            g = m.groups()
            rows.append(dict(obj=g[0], skel=g[1], arch=g[2], cmb=g[3], skip=g[4], reviewed=g[5] == 'true', light=float(g[6]),
                             sat=float(g[7]), rig=g[8], face=g[9], matcls=g[10], tint=g[11]))
    return rows


# ------------------------------------------------------------------ N64 skeleton (from mm.o2r)
def nstr(d, p):
    n = struct.unpack_from('<I', d, p)[0]
    return d[p + 4:p + 4 + n].decode(), p + 4 + n


def read_skeleton(z, path):
    d = z.read(path)
    p, count = 64 + 15, struct.unpack_from('<I', d, 64 + 11)[0]
    table = []
    for _ in range(count):
        s, p = nstr(d, p)
        table.append(s)
    limbs = []
    for s in table:
        b = z.read(s)
        p = 64 + 2
        _, p = nstr(b, p)
        p += 2 + 4
        _, p = nstr(b, p)
        p += 12 + 6
        _, p = nstr(b, p)
        _, p = nstr(b, p)
        dl, p = nstr(b, p)
        dl2, p = nstr(b, p)
        t = struct.unpack_from('<hhh', b, p)
        c, sb = struct.unpack_from('<BB', b, p + 6)
        limbs.append(dict(trans=t, child=c, sibling=sb, dl=dl, dl2=dl2, parent=-1))
    for i, l in enumerate(limbs):
        c = l['child']
        while c != 255:
            limbs[c]['parent'] = i
            c = limbs[c]['sibling']
    return limbs


def matrix_slots(limbs):
    slots, n = {}, 0

    def visit(i):
        nonlocal n
        if limbs[i]['dl']:
            slots[i] = n
            n += 1
        if limbs[i]['child'] != 255:
            visit(limbs[i]['child'])
        if limbs[i]['sibling'] != 255:
            visit(limbs[i]['sibling'])
    visit(0)
    return slots


# ------------------------------------------------------------------ bone to limb (the converter's rules)
def rot_xyz(x, y, z):
    cx, cy, cz, sx, sy, sz = np.cos(x), np.cos(y), np.cos(z), np.sin(x), np.sin(y), np.sin(z)
    return (np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]) @ np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]) @
            np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]]))


def bone_world(bones):
    byid = {b['id']: b for b in bones}
    cache = {}

    def w(i):
        if i in cache:
            return cache[i]
        b = byid[i]
        m = np.eye(4)
        m[:3, :3] = rot_xyz(*b['rotation']) @ np.diag(b['scale'])
        m[:3, 3] = b['translation']
        if b['parent'] >= 0:
            m = w(b['parent']) @ m
        cache[i] = m
        return m
    return {b['id']: w(b['id']) for b in bones}


def match_bones(limbs, bones):
    kids, lkids, byid = {}, {}, {b['id']: b for b in bones}
    for b in bones:
        kids.setdefault(b['parent'], []).append(b['id'])
    for i, l in enumerate(limbs):
        lkids.setdefault(l['parent'], []).append(i)

    def canon(ch, n):
        return tuple(sorted(canon(ch, c) for c in ch.get(n, [])))
    out = {}

    def walk(bone, limb):
        out[bone] = limb
        bk, lk = kids.get(bone, []), list(lkids.get(limb, []))
        for c in bk:
            shape = canon(kids, c)
            cand = [j for j in lk if canon(lkids, j) == shape]
            t = np.array(byid[c]['translation'])
            best = min(cand, key=lambda j: np.linalg.norm(t - np.array(limbs[j]['trans'])))
            lk.remove(best)
            walk(c, best)
    walk(kids[-1][0], lkids[-1][0])
    return out


def parse_rig(s):
    frame, drive = {}, {}
    for tok in filter(None, s.split(',')):
        a, b = tok.split(':')
        if '>' in b:
            b, d = b.split('>')
            drive[int(a)] = int(d)
        frame[int(a)] = int(b)
    return frame, drive


def limb_matrices(row, limbs, bones):
    """drive limb -> 4x4 matrix that puts a converted vertex back at its 3DS bind position (the converter's inverse bind,
    undoing the rest shift and the stature scale), plus the stature scale."""
    rigged = bool(row['rig'])
    W = bone_world(bones)
    if rigged:
        frame, drive = parse_rig(row['rig'])
    else:
        frame, drive = match_bones(limbs, bones), {}
    l2b = {l: b for b, l in frame.items()}
    for b, l in drive.items():
        l2b[l] = b
    k = 1.0
    if rigged and 0 in l2b and W[l2b[0]][1, 3] > 1 and limbs[0]['trans'][1] > 1:
        k = limbs[0]['trans'][1] / W[l2b[0]][1, 3]
    Wk = {b: m.copy() for b, m in W.items()}
    for m in Wk.values():
        m[:3, 3] *= k
    byid = {b['id']: b for b in bones}
    mats = {}
    for b, l in frame.items():
        s = np.zeros(3)
        if rigged:
            pl, dn = limbs[l]['parent'], np.array(limbs[l]['trans'], float)
            while pl >= 0 and pl not in l2b:
                dn = dn + np.array(limbs[pl]['trans'], float)
                pl = limbs[pl]['parent']
            if pl >= 0:
                rel = np.linalg.inv(Wk[l2b[pl]]) @ Wk[b]
                d = dn - rel[:3, 3]
                if np.linalg.norm(d) > 1:
                    s = -rel[:3, :3].T @ d
        elif byid[b]['parent'] >= 0:
            d = np.array(limbs[l]['trans'], float) - np.array(byid[b]['translation'], float)
            if np.linalg.norm(d) > 1:
                s = -rot_xyz(*byid[b]['rotation']).T @ d
        m = Wk[b].copy()
        m[:3, 3] = Wk[b][:3, 3] - Wk[b][:3, :3] @ s
        mats[b] = m
    out = {}
    for b, l in frame.items():
        out[l] = mats[b]
    for b, l in drive.items():
        out[l] = mats[b]
    return out, k


# ------------------------------------------------------------------ rasteriser
class Camera:
    def __init__(self, view, lo, hi, size):
        # view: 'front' looks down -z (camera at +z), 'side' from +x, 'back' from -z
        c = {'front': ((1, 0, 0), (0, 1, 0), (0, 0, 1)), 'back': ((-1, 0, 0), (0, 1, 0), (0, 0, -1)),
             'side': ((0, 0, -1), (0, 1, 0), (1, 0, 0))}[view]
        self.R = np.array(c[0], float)
        self.U = np.array(c[1], float)
        self.T = np.array(c[2], float)  # towards the camera
        self.view = view
        ext = max(hi - lo) * 1.06
        self.scale = size / ext
        self.centre = (lo + hi) / 2
        self.size = size

    def project(self, P):
        d = P - self.centre
        return np.stack([self.size / 2 + (d @ self.R) * self.scale, self.size / 2 - (d @ self.U) * self.scale, d @ self.T], 1)


def rasterise(cam, tris):
    """tris: (n,3,3) world positions. -> tri id (-1 none), barycentrics (H,W,3), depth."""
    S = cam.size
    tid = np.full((S, S), -1, np.int32)
    bary = np.zeros((S, S, 3), np.float32)
    zbuf = np.full((S, S), -1e30, np.float32)
    P = np.stack([cam.project(t) for t in tris])
    for i, t in enumerate(P):
        x0, y0, x1, y1 = int(max(t[:, 0].min() - 1, 0)), int(max(t[:, 1].min() - 1, 0)), int(min(t[:, 0].max() + 2, S)), int(min(t[:, 1].max() + 2, S))
        if x1 <= x0 or y1 <= y0:
            continue
        den = (t[1, 1] - t[2, 1]) * (t[0, 0] - t[2, 0]) + (t[2, 0] - t[1, 0]) * (t[0, 1] - t[2, 1])
        if abs(den) < 1e-9:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        a = ((t[1, 1] - t[2, 1]) * (xs - t[2, 0]) + (t[2, 0] - t[1, 0]) * (ys - t[2, 1])) / den
        b = ((t[2, 1] - t[0, 1]) * (xs - t[2, 0]) + (t[0, 0] - t[2, 0]) * (ys - t[2, 1])) / den
        c = 1 - a - b
        inside = (a >= -1e-4) & (b >= -1e-4) & (c >= -1e-4)
        if not inside.any():
            continue
        z = a * t[0, 2] + b * t[1, 2] + c * t[2, 2]
        sub = zbuf[y0:y1, x0:x1]
        win = inside & (z > sub)
        sub[win] = z[win]
        tid[y0:y1, x0:x1][win] = i
        bb = bary[y0:y1, x0:x1]
        bb[win] = np.stack([a, b, c], -1)[win]
    return tid, bary, zbuf


def srgb_to_lab(rgb):
    c = np.clip(rgb, 0, 1)
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = lin @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def sample(tex, uv, wrap_s=10497, wrap_t=10497):
    """tex (h,w,4) float, uv (n,2) with v up; bilinear, repeat or clamp."""
    h, w = tex.shape[:2]
    x = uv[:, 0] * w - 0.5
    y = (1 - uv[:, 1]) * h - 0.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]

    def wrap(i, n, mode):
        if mode == 33071:
            return np.clip(i, 0, n - 1)
        if mode == 33648:
            j = np.mod(i, 2 * n)
            return np.where(j >= n, 2 * n - 1 - j, j)
        return np.mod(i, n)
    xa, xb = wrap(x0, w, wrap_s), wrap(x0 + 1, w, wrap_s)
    ya, yb = wrap(y0, h, wrap_t), wrap(y0 + 1, h, wrap_t)
    return (tex[ya, xa] * (1 - fx) * (1 - fy) + tex[ya, xb] * fx * (1 - fy) + tex[yb, xa] * (1 - fx) * fy + tex[yb, xb] * fx * fy)


# ------------------------------------------------------------------ the 3DS reference shader
def operand(v, op):
    """v (n,4) rgba; op: GL operand enum -> (n,3) for colour or (n,1) for alpha"""
    if op == 0x0300:
        return v[:, :3]
    if op == 0x0301:
        return 1 - v[:, :3]
    if op == 0x0302:
        return np.repeat(v[:, 3:4], 3, 1)
    if op == 0x0303:
        return np.repeat(1 - v[:, 3:4], 3, 1)
    if op in (0x8860, 0x8861, 0x8862, 0x8863, 0x8864, 0x8865):
        ch = (op - 0x8860) // 2
        x = np.repeat(v[:, ch:ch + 1], 3, 1)
        return 1 - x if (op - 0x8860) & 1 else x
    return v[:, :3]


def combine(fn, a, b, c):
    if fn == 0x1E01:
        return a
    if fn == 0x2100:
        return a * b
    if fn == 0x0104:
        return a + b
    if fn == 0x8574:
        return a + b - 0.5
    if fn == 0x8575:
        return a * c + b * (1 - c)
    if fn == 0x84E7:
        return a - b
    if fn == 0x6401:
        return a * b + c
    if fn == 0x6402:
        return (a + b) * c
    if fn in (0x86AE, 0x86AF):
        d = np.sum((a - 0.5) * (b - 0.5), 1, keepdims=True) * 4
        return np.repeat(d, 3, 1)
    return a


def shade_material(mat, textures, uvs, normal, frag_primary, vcol, warnings):
    """Evaluate the material's combiner stages -> (n,4) rgba, alpha included."""
    n = len(normal)
    texv = []
    for j in range(3):
        ti = mat['bindings'][j]['texture']
        if ti < 0:
            texv.append(np.zeros((n, 4)))
            continue
        co = mat['coordinators'][j]
        uv = uvs.copy()
        sx, sy, rot, tx, ty = co['transform']
        if co['mapping'] == 1 or co['mapping'] == 0:
            uv = uv * np.array([sx, sy])
            if rot:
                c, s = np.cos(rot), np.sin(rot)
                uv = np.stack([c * uv[:, 0] - s * uv[:, 1], s * uv[:, 0] + c * uv[:, 1]], 1)
            uv = uv + np.array([tx, ty])
        else:
            warnings.add('texture mapping %d not evaluated' % co['mapping'])
            texv.append(np.zeros((n, 4)))
            continue
        b = mat['bindings'][j]
        texv.append(sample(textures[ti], uv, b['wrap_s'], b['wrap_t']))
    prev = np.zeros((n, 4))
    prevbuf = np.zeros((n, 4))
    frag = np.concatenate([frag_primary, np.ones((n, 1))], 1)
    prim = vcol
    sec = np.zeros((n, 4))
    out = prev
    for st in mat['combiners']:
        fn_rgb, fn_a, sc_rgb, sc_a = st[0], st[1], st[2], st[3]
        s_rgb, op_rgb, s_a, op_a = st[6:9], st[9:12], st[12:15], st[15:18]
        const = np.tile(np.array(mat['constants'][st[18]], float) / 255.0, (n, 1)) if st[18] < len(mat['constants']) else np.zeros((n, 4))

        def source(s):
            if s in SRC_TEX:
                return texv[SRC_TEX[s]] if SRC_TEX[s] < 3 else np.zeros((n, 4))
            if s == SRC_CONSTANT:
                return const
            if s == SRC_PRIMARY:
                return prim
            if s == SRC_PREVIOUS:
                return prev
            if s == SRC_PREV_BUFFER:
                return prevbuf
            if s == SRC_FRAG_PRIMARY:
                return frag
            if s == SRC_FRAG_SECONDARY:
                return sec
            warnings.add('source %#x unknown' % s)
            return np.zeros((n, 4))
        a = [operand(source(s_rgb[i]), op_rgb[i]) for i in range(3)]
        rgb = np.clip(combine(fn_rgb, *a) * sc_rgb, 0, 1)
        aa = [operand(source(s_a[i]), op_a[i])[:, :1] for i in range(3)]
        alpha = np.clip(combine(fn_a, *[np.repeat(x, 3, 1) for x in aa])[:, :1] * sc_a, 0, 1)
        prevbuf = prev
        prev = np.concatenate([rgb, alpha], 1)
    return prev


def render_reference(model, mats_by_draw_scale, cam, lights, k, warnings):
    V, I = model['vertices'], model['indices']
    tris, meta = [], []
    for di, dr in enumerate(model['draws']):
        T = I[dr['first_index']:dr['first_index'] + dr['index_count']].reshape(-1, 3)
        tris.append(V[T][:, :, :])
        meta += [(di, dr['material'])] * len(T)
    vt = np.concatenate(tris)  # (n,3,12)
    pos = vt[:, :, :3] * k
    tid, bary, _ = rasterise(cam, pos)
    H = cam.size
    img = np.zeros((H, H, 3), np.float32)
    unit = np.full((H, H), -1, np.int32)
    ys, xs = np.nonzero(tid >= 0)
    ids = tid[ys, xs]
    b = bary[ys, xs]
    nrm = np.einsum('nk,nkc->nc', b, vt[ids][:, :, 3:6])
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    uv = np.einsum('nk,nkc->nc', b, vt[ids][:, :, 6:8])
    col = np.einsum('nk,nkc->nc', b, vt[ids][:, :, 8:12])
    textures = [np.asarray(t['levels'][0][0] if isinstance(t['levels'][0], list) else t['levels'][0], float) / 255.0 for t in model['textures']]
    mat_of = np.array([m for _, m in meta])[ids]
    alpha_out = np.ones(len(ids))
    colour = np.zeros((len(ids), 3))
    for mi in np.unique(mat_of):
        sel = mat_of == mi
        mat = model['materials'][mi]
        amb = np.array(mat['ambient'][:3]) / 255.0
        dif = np.array(mat['diffuse'][:3]) / 255.0
        emi = np.array(mat['emission'][:3]) / 255.0
        n = nrm[sel]
        lam = np.zeros((sel.sum(), 3))
        lam += emi + amb * lights['ambient']
        for L, strength in lights['dirs']:
            lam += dif * strength * np.maximum(n @ L, 0)[:, None]
        frag = np.clip(lam, 0, 1) if mat['fragment_lighting'] else np.ones_like(lam)
        out = shade_material(mat, textures, uv[sel], n, frag, col[sel], warnings)
        colour[sel] = out[:, :3]
        alpha_out[sel] = out[:, 3]
        first = next((bd['texture'] for bd in mat['bindings'] if bd['texture'] >= 0), -1)
        unit[ys[sel], xs[sel]] = first
    img[ys, xs] = colour
    return img, unit, tid >= 0


# ------------------------------------------------------------------ the converted side
class Converted:
    def __init__(self, cache, o2r, row, model):
        self.z = zipfile.ZipFile(cache)
        self.names = {crc64(n): n for n in self.z.namelist()}
        zm = zipfile.ZipFile(o2r)
        self.limbs = read_skeleton(zm, 'objects/%s/%s' % (row['obj'], row['skel']))
        self.slots = {v: k for k, v in matrix_slots(self.limbs).items()}
        self.mats, self.k = limb_matrices(row, self.limbs, model['bones'])
        self.row = row
        self.model = model
        self.ignore_row_colour = False
        self.whatif = None  # (light reference, saturation): rebuild the textures from the CMB with these colour constants
        self.cache_tex = {}
        self.warn = set()
        self.seg_tex = {}
        for part in filter(None, row['face'].split('|')):
            f = part.split(':')
            first = f[3].split(',')[0].split('=')[0]
            self.seg_tex[int(f[1])] = 'mm3d/objects/%s/%s' % (row['obj'], first)

    def texture(self, name):
        if self.whatif is not None:
            m = re.search(r'_tex(\d+)$', name)
            if m:
                return self.whatif_texture(int(m.group(1)))
        d = self.z.read(name)
        w, h = struct.unpack_from('<II', d, 64 + 4)
        n = struct.unpack_from('<I', d, 64 + 24)[0]
        return np.frombuffer(d, np.uint8, n, 64 + 28).reshape(h, w, 4).astype(float) / 255.0

    def whatif_texture(self, ti):
        if ti in self.cache_tex:
            return self.cache_tex[ti]
        t = self.model['textures'][ti]
        lv = t['levels'][0]
        img = np.asarray(lv[0] if isinstance(lv, list) else lv, float)
        mat = next((m for m in self.model['materials'] if next((b['texture'] for b in m['bindings'] if b['texture'] >= 0), -1) == ti), None)
        light, sat = self.whatif
        if not self.ignore_row_colour:
            light = self.row['light'] or light
            sat = self.row['sat'] or sat
        coeff = (0.6 * mat['diffuse'][0] + 0.4 * mat['ambient'][0]) / 255.0 if mat else 0.5
        scale = 1.0
        for st in (mat['combiners'] if mat else []):
            scale *= st[2] if st[2] in (2, 4) else 1.0
        lit = mat and (mat['fragment_lighting'] or mat['vertex_lighting'])
        gain = (scale * coeff / light if coeff > 0.05 else 1.0) if lit else scale
        rgb = img[..., :3]
        lum = (0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2])[..., None]
        out = np.clip((lum + (rgb - lum) * sat) * gain, 0, 255)
        res = np.concatenate([out, img[..., 3:4]], -1) / 255.0
        self.cache_tex[ti] = res
        return res

    def triangles(self):
        """-> list of dict(limb, pos (3,3) bind world, nrm (3,3), st (3,2) in texels/32, dw, dh, tex name, lit, alpha)"""
        out = []
        for li, limb in enumerate(self.limbs):
            if not limb['dl']:
                continue
            name = 'mm3d/' + limb['dl']
            if name not in self.z.namelist():
                continue
            d = self.z.read(name)
            p = 64 + 8
            vb = [None] * 64
            cur = dict(tex=None, dw=32, dh=32, lit=True, mode=0xC8112078)
            cur_limb = li
            while p + 8 <= len(d):
                w0, w1 = struct.unpack_from('>II', d, p) if False else struct.unpack_from('<II', d, p)
                p += 8
                op = w0 >> 24
                ex = None
                if op in (0x20, 0x31, 0x32, 0x33, 0x35, 0x36, 0x42):
                    a, b = struct.unpack_from('<II', d, p)
                    p += 8
                    ex = (a << 32) | b
                if op == 0xDF:
                    break
                if op == 0xD9:
                    cur['lit'] = bool(w1 & 0x20000)
                elif op == 0xE2:
                    cur['mode'] = w1
                elif op == 0x20:
                    cur['tex'] = self.names.get(ex)
                elif op == 0xFD:
                    cur['tex'] = self.seg_tex.get(w1 >> 24)
                elif op == 0xF2:
                    cur['dw'], cur['dh'] = ((w1 >> 12) & 0xFFF) // 4 + 1, (w1 & 0xFFF) // 4 + 1
                elif op == 0xDA and (w1 >> 24) == 0x0D:
                    cur_limb = self.slots[(w1 & 0xFFFFFE) // 0x40]
                elif op == 0x32:
                    n, end = (w0 >> 12) & 0xFF, (w0 >> 1) & 0x7F
                    arr = self.z.read(self.names[ex])
                    for i in range(n):
                        o = 64 + 8 + w1 + 16 * i
                        x, y, zz = struct.unpack_from('<hhh', arr, o)
                        s, t = struct.unpack_from('<hh', arr, o + 8)
                        nx, ny, nz = struct.unpack_from('<bbb', arr, o + 12)
                        vb[end - n + i] = (cur_limb, (x, y, zz), (nx / 127.0, ny / 127.0, nz / 127.0), (s / 32.0, t / 32.0))
                    # the matrix that owns these vertices is the one loaded when they were
                elif op in (5, 6):
                    tris = [((w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF)]
                    if op == 6:
                        tris.append(((w1 >> 16) & 0xFF, (w1 >> 8) & 0xFF, w1 & 0xFF))
                    for t in tris:
                        vs = [vb[i // 2] for i in t]
                        if any(v is None for v in vs):
                            continue
                        out.append(dict(limbs=[v[0] for v in vs], pos=np.array([v[1] for v in vs], float), nrm=np.array([v[2] for v in vs]),
                                        st=np.array([v[3] for v in vs]), **cur))
        return out


def render_converted(conv, cam, lights, ambient, diffuse, warnings):
    tris = conv.triangles()
    world, nrm = [], []
    for t in tris:
        wp, wn = [], []
        for l, p, n in zip(t['limbs'], t['pos'], t['nrm']):
            M = conv.mats[l]
            wp.append(M[:3, :3] @ p + M[:3, 3])
            wn.append(M[:3, :3] @ n)
        world.append(wp)
        nrm.append(wn)
    world = np.array(world)
    nrm = np.array(nrm)
    tid, bary, _ = rasterise(cam, world)
    H = cam.size
    img = np.zeros((H, H, 3), np.float32)
    unit = np.full((H, H), -1, np.int32)
    ys, xs = np.nonzero(tid >= 0)
    ids = tid[ys, xs]
    b = bary[ys, xs]
    n = np.einsum('nk,nkc->nc', b, nrm[ids])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    st = np.einsum('nk,nkc->nc', b, np.array([t['st'] for t in tris])[ids])
    texname = np.array([t['tex'] or '' for t in tris])[ids]
    lit = np.array([t['lit'] for t in tris])[ids]
    dwh = np.array([[t['dw'], t['dh']] for t in tris])[ids]
    colour = np.zeros((len(ids), 3))
    name_to_unit = {}
    for tn in np.unique(texname):
        sel = texname == tn
        if not tn:
            warnings.add('triangles without a texture')
            continue
        tex = conv.texture(tn)
        uv = np.stack([st[sel, 0] / dwh[sel, 0], 1 - st[sel, 1] / dwh[sel, 1]], 1)
        t = sample(tex, uv, 10497, 10497)[:, :3]
        shade = np.where(lit[sel], ambient + diffuse * np.maximum(n[sel] @ lights['dirs'][0][0], 0), 1.0)[:, None]
        colour[sel] = np.clip(t * shade, 0, 1)
        m = re.search(r'_tex(\d+)$', tn)
        name_to_unit[tn] = int(m.group(1)) if m else tn
        unit[ys[sel], xs[sel]] = name_to_unit[tn] if isinstance(name_to_unit[tn], int) else -2
    img[ys, xs] = colour
    return img, unit, tid >= 0


# ------------------------------------------------------------------ comparison
def bands(mask):
    ys = np.nonzero(mask.any(1))[0]
    if not len(ys):
        return {}
    top, bot = ys.min(), ys.max()
    h = bot - top + 1
    out = {'height': int(h)}
    for name, (a, b) in (('head', (0.0, 0.25)), ('torso', (0.25, 0.65)), ('legs', (0.65, 1.0))):
        r0, r1 = top + int(h * a), top + int(h * b)
        xs = np.nonzero(mask[r0:r1].any(0))[0]
        out[name] = int(xs.max() - xs.min() + 1) if len(xs) else 0
    xs = np.nonzero(mask.any(0))[0]
    out['width'] = int(xs.max() - xs.min() + 1)
    return out


def compare(model, conv, rows_ref, rows_cv, view_names):
    report = {'units': {}, 'views': {}}
    names = [t['name'] for t in model['textures']]
    for vi, view in enumerate(view_names):
        ri, ru, rm = rows_ref[vi]
        ci, cu, cm = rows_cv[vi]
        both = rm & cm
        iou = (rm & cm).sum() / max((rm | cm).sum(), 1)
        report['views'][view] = {'iou': float(iou), 'ref': bands(rm), 'conv': bands(cm)}
        for u in np.unique(ru[rm]):
            if u < 0:
                continue
            if any(m['alpha_mode'] == 'blend' and next((b['texture'] for b in m['bindings'] if b['texture'] >= 0), -1) == u
                   for m in model['materials']):
                report.setdefault('blend', set()).add(names[u])  # alpha-blended (eyes, glows): the mean depends on the background
                continue
            m = (ru == u) & (cu == u) & both
            if m.sum() < 40:
                continue
            la, lb = srgb_to_lab(ri[m]).mean(0), srgb_to_lab(ci[m]).mean(0)
            rec = report['units'].setdefault(names[u], {'dE': [], 'ref': [], 'conv': [], 'px': 0})
            rec['dE'].append(float(np.linalg.norm(la - lb)))
            rec['ref'].append(ri[m].mean(0))
            rec['conv'].append(ci[m].mean(0))
            rec['px'] += int(m.sum())
    return report


def sheet(row, rows_ref, rows_cv, view_names, path, label):
    S = rows_ref[0][0].shape[0]
    img = Image.new('RGB', (S * 3, S * len(view_names) + 20), (30, 30, 34))
    d = ImageDraw.Draw(img)
    d.text((4, 4), label + '   reference (3DS combiners) | converted (N64 lights) | difference x3', fill=(255, 255, 255))
    for vi in range(len(view_names)):
        a, b = rows_ref[vi][0], rows_cv[vi][0]
        for ci, im in enumerate((a, b, np.clip(np.abs(a - b) * 3, 0, 1))):
            bg = np.full_like(im, 0.12)
            m = (rows_ref[vi][2] | rows_cv[vi][2])[..., None]
            img.paste(Image.fromarray((np.where(m, im, bg) * 255).astype(np.uint8)), (ci * S, 20 + vi * S))
    img.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--romfs', default=str(Path.home() / 'dev/ghm-mods/mm3d/romfs'))
    ap.add_argument('--o2r', default=str(Path.home() / 'dev/2ship2harkinian/build-cmake/mm/mm.o2r'))
    ap.add_argument('--cache', required=True, help='the generated mm3d-characters.o2r (a game run with the option on writes it)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--object')
    ap.add_argument('--skeleton')
    ap.add_argument('--enabled', action='store_true', help='all rows enabled in game (reviewed rows)')
    ap.add_argument('--size', type=int, default=360)
    ap.add_argument('--ref-ambient', type=float, default=0.6)
    ap.add_argument('--ref-diffuse', type=float, default=0.8)
    ap.add_argument('--n64-ambient', type=float, default=0.55)
    ap.add_argument('--n64-diffuse', type=float, default=0.65)
    ap.add_argument('--whatif', nargs=2, type=float, metavar=('LIGHTREF', 'SATURATION'), help='rebuild the converted textures from the CMB with these colour constants (the table rows keep their own overrides); LIGHTREF is the N64 actor light level the textures are scaled for')
    ap.add_argument('--ignore-row-colour', action='store_true', help='with --whatif: also ignore the per-model colour overrides of the table')
    ap.add_argument('--views', default='front,side,back')
    ap.add_argument('--sweep', action='store_true', help='print the mean delta E over the selected rows for a grid of colour constants')
    ap.add_argument('--tol-de', type=float, default=12.0)
    ap.add_argument('--tol-extent', type=float, default=0.12)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    rows = [r for r in read_table() if (not a.object or r['obj'] == a.object) and (not a.skeleton or r['skel'] == a.skeleton)]
    if a.enabled:
        rows = [r for r in rows if r['reviewed']]
    views = a.views.split(',')
    failed = False
    sweep_rows = []
    for row in rows:
        gar = binary.gar(Path(a.romfs, 'actors', row['arch'] + '.gar.lzs').read_bytes())
        model = cmb.parse(gar[row['cmb']], decode_textures=True)
        draws_skip = {int(t[1:]) for t in row['skip'].split(',') if t.startswith('d')}
        mats_skip = {int(t) for t in row['skip'].split(',') if t and t[0].isdigit()}
        keep = [i for i, dr in enumerate(model['draws']) if i not in draws_skip and dr['material'] not in mats_skip]
        model['draws'] = [model['draws'][i] for i in keep]
        zm = zipfile.ZipFile(a.o2r)
        limbs = read_skeleton(zm, 'objects/%s/%s' % (row['obj'], row['skel']))
        _, k = limb_matrices(row, limbs, model['bones'])
        conv = Converted(a.cache, a.o2r, row, model)
        conv.whatif = tuple(a.whatif) if a.whatif else None
        conv.ignore_row_colour = a.ignore_row_colour
        V = model['vertices']
        allp = np.concatenate([V[model['indices'][dr['first_index']:dr['first_index'] + dr['index_count']], :3] for dr in model['draws']]) * k
        lo, hi = allp.min(0), allp.max(0)
        warn_ref, warn_cv = set(), set()
        ref_rows, cv_rows = [], []
        for view in views:
            cam = Camera(view, lo, hi, a.size)
            key = (-cam.R * 0.35 + cam.U * 0.45 + cam.T * 0.82)
            key /= np.linalg.norm(key)
            lights = {'ambient': a.ref_ambient, 'dirs': [(key, a.ref_diffuse)]}
            ref_rows.append(render_reference(model, None, cam, lights, k, warn_ref))
            n64 = {'dirs': [(key, 1.0)]}
            cv_rows.append(render_converted(conv, cam, n64, a.n64_ambient, a.n64_diffuse, warn_cv))
        rep = compare(model, conv, ref_rows, cv_rows, views)
        tag = '%s_%s' % (row['obj'], row['skel'])
        sheet(row, ref_rows, cv_rows, views, out / (tag + '.png'), tag)
        print('== %s (%s)' % (tag, row['cmb']))
        for w in sorted(warn_ref | warn_cv):
            print('   warning:', w)
        for v in views:
            r = rep['views'][v]
            ext = {k2: (r['conv'].get(k2, 0) / r['ref'][k2]) if r['ref'].get(k2) else 1.0 for k2 in ('head', 'torso', 'legs', 'width', 'height')}
            bad = [k2 for k2, x in ext.items() if abs(x - 1) > a.tol_extent]
            print('   %-5s IoU %.2f  extents conv/ref %s%s' % (v, r['iou'], ' '.join('%s %.2f' % kv for kv in ext.items()),
                                                           '   <-- OUT OF TOLERANCE: ' + ','.join(bad) if bad else ''))
            failed |= bool(bad)
        for name in sorted(rep.get('blend', ())):
            print('   %-22s alpha-blended: not compared' % name)
        for name, rec in rep['units'].items():
            de = float(np.mean(rec['dE']))
            ref, cvv = np.mean(rec['ref'], 0), np.mean(rec['conv'], 0)
            flag = '' if de <= a.tol_de else '   <-- dE over tolerance'
            failed |= de > a.tol_de
            print('   %-22s dE %5.1f  ref RGB %s  conv RGB %s  (%d px)%s' % (name, de, (ref * 255).round(0).astype(int).tolist(),
                                                                             (cvv * 255).round(0).astype(int).tolist(), rec['px'], flag))
    print('GATE', 'FAILED' if failed else 'passed', '(%d rows)' % len(rows))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
