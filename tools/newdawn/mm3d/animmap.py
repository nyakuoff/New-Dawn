#!/usr/bin/env python3
"""Map N64 animated-material segments onto converted MM3D materials, by geometry.

For each scene the N64 room triangles drawn after a segment DL call (n64anim.animated_triangles) are
compared with the triangles of every MM3D material (world space, same coordinates). A material belongs
to a segment when most of its area lies on that segment's N64 surface; a segment is mapped when most
of its N64 surface is covered by its materials. Output per scene: <out>/<scene>/anim-map.json (read by
the game) plus a summary <out>/anim-map-report.json (read by allowlist.py).

  animmap.py [--out ~/dev/ghm-mods/mm3d/out] [--o2r mm.o2r] [scene ...]
"""
import argparse
import json
import struct
import zipfile
from pathlib import Path

import numpy as np

import n64anim

PLANE_EPS = 3.0      # world units between the MM3D point and the N64 triangle plane
EDGE_TOL = 0.05      # barycentric slack at triangle edges
MATERIAL_MIN = 0.8   # area share of a material that must sit on one segment
SEGMENT_MIN = 0.3   # share of a segment's N64 surface covered by its materials (MM3D rooms may be smaller)


def load_mesh(path):
    d = path.read_bytes()
    _, nj, nv, ni, stride = struct.unpack_from('<8s4I', d)
    meta = json.loads(d[24:24 + nj])
    verts = np.frombuffer(d, '<f4', nv * 12, 24 + nj).reshape(-1, 12)[:, :3]
    inds = np.frombuffer(d, '<u4', ni, 24 + nj + nv * 48)
    return meta, verts, inds


def on_surface(points, tris, chunk=2_000_000):
    """bool per point: lies within PLANE_EPS of one of the triangles (and inside it, with slack)."""
    if len(points) == 0 or len(tris) == 0:
        return np.zeros(len(points), bool)
    a, b, c = (tris[:, i].astype('f8') for i in range(3))
    n = np.cross(b - a, c - a)
    area2 = np.linalg.norm(n, axis=1)
    keep = area2 > 1e-6
    a, b, c, n, area2 = a[keep], b[keep], c[keep], n[keep], area2[keep]
    n /= area2[:, None]
    out = np.zeros(len(points), bool)
    step = max(1, chunk // max(len(a), 1))
    for i in range(0, len(points), step):
        p = points[i:i + step].astype('f8')[:, None, :]
        dist = np.abs(((p - a[None]) * n[None]).sum(2))
        v0, v1, v2 = (b - a)[None], (c - a)[None], p - a[None]
        d00, d01, d11 = (v0 * v0).sum(2), (v0 * v1).sum(2), (v1 * v1).sum(2)
        d20, d21 = (v2 * v0).sum(2), (v2 * v1).sum(2)
        den = d00 * d11 - d01 * d01
        den = np.where(np.abs(den) < 1e-9, 1e-9, den)
        u = (d11 * d20 - d01 * d21) / den
        v = (d00 * d21 - d01 * d20) / den
        ok = (dist < PLANE_EPS) & (u > -EDGE_TOL) & (v > -EDGE_TOL) & (u + v < 1 + EDGE_TOL)
        out[i:i + step] = ok.any(1)
    return out


def triangle_samples(tris):
    """centroid plus three points pulled toward the corners, per triangle; areas repeated per sample."""
    cen = tris.mean(1)
    pts = [cen] + [cen + 0.5 * (tris[:, i] - cen) for i in range(3)]
    area = 0.5 * np.linalg.norm(np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]), axis=1)
    return np.concatenate(pts), np.tile(area, 4)


def map_scene(z, out_root, scene):
    anims = n64anim.scene_animations(z, scene.upper())
    report = {'scene': scene, 'segments': {}, 'ok': True, 'notes': []}
    if not anims:
        return report, {}
    n64 = n64anim.animated_triangles(z, scene.upper())
    meshes = {p.name: load_mesh(p) for p in sorted((out_root / scene).glob('*.ndmesh'))}
    material_map = {}
    for seg, entry in sorted(anims.items()):
        info = {'type': entry['type'], 'mapped_materials': 0, 'coverage': 0.0}
        report['segments'][str(seg)] = info
        if entry['type'] not in n64anim.SCROLL_TYPES:
            info['unsupported'] = 'animation type %d' % entry['type']
            report['ok'] = False
            continue
        tris = n64.get(seg)
        if tris is None or len(tris) == 0:
            info['unsupported'] = 'no N64 triangles use this segment'
            continue  # animated but never drawn by a room: nothing to show, nothing to map
        matched = []  # world triangles of the materials mapped to this segment
        for name, (meta, verts, inds) in meshes.items():
            for mat in meta['materials']:
                idx = mat['index']
                ranges = [dr for dr in meta['draws'] if dr['material'] == idx]
                if not ranges:
                    continue
                ids = np.concatenate([inds[r['first_index']:r['first_index'] + r['index_count']] for r in ranges])
                mt = verts[ids].reshape(-1, 3, 3)
                pts, area = triangle_samples(mt)
                hit = on_surface(pts, tris)
                share = float(area[hit].sum() / max(area.sum(), 1e-9))
                if share >= MATERIAL_MIN:
                    material_map.setdefault(name, {})[str(idx)] = seg
                    matched.append(mt)
                    info['mapped_materials'] += 1
                elif share > 0.05:
                    report['notes'].append('%s material %d: only %.0f%% on segment %d' % (name[:8], idx, 100 * share, seg))
        if matched:
            pts, area = triangle_samples(tris)
            hit = on_surface(pts, np.concatenate(matched))
            info['coverage'] = float(area[hit].sum() / max(area.sum(), 1e-9))
        if info['coverage'] < SEGMENT_MIN:
            report['ok'] = False
    return report, material_map


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('scenes', nargs='*')
    ap.add_argument('--out', type=Path, default=Path.home() / 'dev/ghm-mods/mm3d/out')
    ap.add_argument('--o2r', type=Path, default=n64anim.DEFAULT_O2R)
    args = ap.parse_args()
    reports = {}
    with zipfile.ZipFile(args.o2r) as z:
        scenes = args.scenes or sorted(s.lower() for s in n64anim.animated_scenes(args.o2r))
        for scene in scenes:
            if not (args.out / scene).is_dir():
                continue
            report, material_map = map_scene(z, args.out, scene)
            (args.out / scene / 'anim-map.json').write_text(
                json.dumps({'format': 'NDMM3DANIM1', 'scene': scene, 'materials': material_map}, indent=1))
            reports[scene] = report
            print('%-18s ok=%-5s %s' % (scene, report['ok'], {k: (v['type'], v['mapped_materials'], round(v['coverage'], 2))
                                                             for k, v in report['segments'].items()}))
    (args.out / 'anim-map-report.json').write_text(json.dumps(reports, indent=1))


if __name__ == '__main__':
    main()
