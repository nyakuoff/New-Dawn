#!/usr/bin/env python3
"""Pack the MM3D ZSI collision mesh of a scene into a runtime override for the N64 collision header.

Only the vertex and polygon lists are replaced. Each MM3D polygon takes the surface type index and the
exclusion/conveyor flag bits of the nearest N64 polygon that faces the same way, so sounds, slopes, void-outs,
hookshot and camera settings keep their N64 meaning. Surface type table, camera list and water boxes stay N64.

Output <out>/<scene>/collision.ndcol (little endian):
  "NDCOL001", u32 nverts, u32 npolys, s16 bounds[6] (min xyz, max xyz),
  s16 verts[nverts][3], polys[npolys]: u16 type, u16 vtxA|flags, u16 vtxB|flags, u16 vtxC, s16 normal[3], s16 dist
and <out>/<scene>/collision-pack-report.json with the differences that matter for gameplay.

  collisionpack.py z2_clocktower [--out DIR] [--romfs DIR] [--o2r mm.o2r]
"""
import argparse
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scene as zsi  # noqa: E402
from binary import Reader, FormatError  # noqa: E402

HOME = Path.home()


def read_n64(data):
    """Full N64 CollisionHeader resource: geometry plus per-polygon type/flags."""
    r = Reader(data)
    if r.u8(0) != 0 or bytes(r.take(4, 4)) != b'LOCO' or r.u32(8) != 0:
        raise FormatError('unsupported N64 collision resource header')
    bounds = list(r.unpack('<6h', 64))
    nv = r.u32(76)
    verts = np.frombuffer(r.take(80, nv * 6), '<i2').reshape(-1, 3)
    p = 80 + nv * 6
    npoly = r.u32(p)
    rec = np.frombuffer(r.take(p + 4, npoly * 16), '<u2').reshape(-1, 8)
    return dict(bounds=bounds, vertices=verts, records=rec, indices=(rec[:, 1:4] & 8191).astype(np.int32),
                normals=rec[:, 4:7].view('<i2').astype(float) / 32767)


def closest_point_on_triangles(p, a, b, c):
    """Ericson, vectorised over triangles for a single point p."""
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = (ab * ap).sum(1), (ac * ap).sum(1)
    bp = p - b
    d3, d4 = (ab * bp).sum(1), (ac * bp).sum(1)
    cp = p - c
    d5, d6 = (ab * cp).sum(1), (ac * cp).sum(1)
    vc, vb, va = d1 * d4 - d3 * d2, d5 * d2 - d1 * d6, d3 * d6 - d5 * d4
    den = np.where(np.abs(va + vb + vc) < 1e-12, 1e-12, va + vb + vc)
    v, w = vb / den, vc / den
    q = a + ab * v[:, None] + ac * w[:, None]
    # clamp to the triangle by projecting onto edges when outside
    out = (v < 0) | (w < 0) | (v + w > 1)
    if out.any():
        best = np.full(len(a), np.inf)
        qq = q.copy()
        for s, e in ((a, b), (b, c), (c, a)):
            se = e - s
            t = np.clip(((p - s) * se).sum(1) / np.maximum((se * se).sum(1), 1e-12), 0, 1)
            pt = s + se * t[:, None]
            dd = ((pt - p) ** 2).sum(1)
            better = out & (dd < best)
            qq[better] = pt[better]
            best = np.where(better, dd, best)
        q = qq
    return np.sqrt(((q - p) ** 2).sum(1))


def pack(scene_name, romfs, o2r, out):
    seg = scene_name.upper()
    with zipfile.ZipFile(o2r) as z:
        names = [n for n in z.namelist() if n.startswith(f'scenes/nonmq/{seg}/{seg}CollisionHeader_') and not n.endswith('.meta')]
        if len(names) != 1:
            raise FormatError('expected one N64 collision resource')
        n64 = read_n64(z.read(names[0]))
    setups = zsi.setups(zsi.parse((romfs / 'scenes' / f'{scene_name}_info.zsi').read_bytes()))
    cols = [s['collision'] for s in setups if 'collision' in s]
    if not cols:
        raise FormatError('no MM3D collision')
    mm = cols[0]
    if any(not np.array_equal(mm['vertices'], c['vertices']) or not np.array_equal(mm['indices'], c['indices']) for c in cols):
        raise FormatError('alternate collision variants differ')
    if len(mm['vertices']) > 8192:
        raise FormatError('too many vertices for 13-bit collision indices')
    mv = mm['vertices']
    tri = mv[mm['indices']]
    ntri = n64['vertices'].astype(float)[n64['indices']]
    na, nb, nc = ntri[:, 0], ntri[:, 1], ntri[:, 2]

    src = np.zeros(len(tri), int)
    gap = np.zeros(len(tri))
    for i, t in enumerate(tri):
        cen = t.mean(0)
        samples = [cen] + [cen + 0.5 * (t[k] - cen) for k in range(3)]
        cand = np.nonzero((n64['normals'] @ mm['normals'][i]) > 0.6)[0]
        if len(cand) == 0:
            cand = np.arange(len(ntri))
        best, bestd = 0, np.inf
        for s in samples:
            d = closest_point_on_triangles(s, na[cand], nb[cand], nc[cand])
            k = int(d.argmin())
            if d[k] < bestd:
                best, bestd = int(cand[k]), float(d[k])
        src[i], gap[i] = best, bestd

    rec = n64['records'][src]
    polys = np.zeros((len(tri), 8), '<u2')
    polys[:, 0] = rec[:, 0]
    polys[:, 1] = mm['indices'][:, 0].astype('<u2') | (rec[:, 1] & 0xE000)
    polys[:, 2] = mm['indices'][:, 1].astype('<u2') | (rec[:, 2] & 0xE000)
    polys[:, 3] = mm['indices'][:, 2].astype('<u2')
    nrm = np.round(mm['normals'] * 32767).astype('<i2')
    polys[:, 4:7] = nrm.view('<u2')
    unit = mm['normals'] / np.maximum(np.linalg.norm(mm['normals'], axis=1, keepdims=True), 1e-9)
    dist = -(unit * tri[:, 0]).sum(1)
    polys[:, 7] = np.round(dist).astype('<i2').view('<u2')

    lo = np.minimum(np.array(n64['bounds'][:3]), mv.min(0))
    hi = np.maximum(np.array(n64['bounds'][3:]), mv.max(0))
    bounds = [int(x) for x in np.concatenate([lo, hi])]
    blob = b'NDCOL001' + struct.pack('<II6h', len(mv), len(polys), *bounds)
    blob += mv.astype('<i2').tobytes() + polys.tobytes()
    (out / scene_name).mkdir(parents=True, exist_ok=True)
    (out / scene_name / 'collision.ndcol').write_bytes(blob)

    def cls(ny):  # 0 floor, 1 wall, 2 ceiling, like BgCheck's slope tests (floor > 0.5)
        return np.where(ny > 0.5, 0, np.where(ny < -0.8, 2, 1))
    mcls, ncls = cls(mm['normals'][:, 1]), cls(n64['normals'][src][:, 1])
    area = 0.5 * np.linalg.norm(np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]), axis=1)
    report = dict(
        scene=scene_name, n64_vertices=len(n64['vertices']), n64_polys=len(n64['indices']),
        mm3d_vertices=len(mv), mm3d_polys=len(tri), bounds_n64=n64['bounds'], bounds_packed=bounds,
        surface_types_used=sorted({int(x) for x in rec[:, 0]}),
        match_gap_units=dict(p50=float(np.percentile(gap, 50)), p90=float(np.percentile(gap, 90)), max=float(gap.max())),
        area_share_gap_over_25=float(area[gap > 25].sum() / area.sum()),
        polys_gap_over_25=int((gap > 25).sum()),
        class_counts_mm3d={k: int((mcls == i).sum()) for i, k in enumerate(['floor', 'wall', 'ceiling'])},
        class_counts_n64={k: int((cls(n64['normals'][:, 1]) == i).sum()) for i, k in enumerate(['floor', 'wall', 'ceiling'])},
        class_disagree_with_matched_n64=int((mcls != ncls).sum()))
    (out / scene_name / 'collision-pack-report.json').write_text(json.dumps(report, indent=1))
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('scene')
    ap.add_argument('--out', type=Path, default=HOME / 'dev/ghm-mods/mm3d/out')
    ap.add_argument('--romfs', type=Path, default=HOME / 'dev/ghm-mods/mm3d/romfs')
    ap.add_argument('--o2r', type=Path, default=HOME / 'dev/2ship2harkinian/build-cmake/mm/mm.o2r')
    args = ap.parse_args()
    print(json.dumps(pack(args.scene, args.romfs, args.o2r, args.out), indent=1))


if __name__ == '__main__':
    main()
