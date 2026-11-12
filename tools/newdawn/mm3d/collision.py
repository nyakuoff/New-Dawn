"""Static N64-vs-MM3D collision study; measurements only, no game mutations."""
from collections import defaultdict
import json
import math
import itertools
import re
from pathlib import Path
import zipfile
import numpy as np
from binary import Reader, FormatError
import scene


def read_n64(data):
    r = Reader(data)
    if r.u8(0) != 0 or bytes(r.take(4, 4)) != b'LOCO' or r.u32(8) != 0:
        raise FormatError('unsupported N64 collision resource header')
    bounds = list(r.unpack('<6h', 64))
    nv = r.u32(76)
    v = np.frombuffer(r.take(80, nv*6), '<i2').reshape(-1, 3).astype(float)
    p = 80+nv*6
    npoly = r.u32(p)
    p += 4
    records = np.frombuffer(r.take(p, npoly*16), '<i2').reshape(-1, 8)
    indices = records[:, 1:4].astype(np.int32) & 8191
    if len(indices) and indices.max() >= len(v):
        raise FormatError('N64 collision index out of bounds')
    p += npoly*16
    for stride in (8, 8, 6, 14):
        n = r.u32(p)
        p += 4
        r.take(p, n*stride)
        p += n*stride
    if p != len(data):
        raise FormatError('unconsumed N64 collision resource bytes')
    return dict(vertices=v, indices=indices, normals=records[:, 4:7].astype(float)/32767, bounds=bounds)


def floor_samples(triangles, spacing):
    """World-aligned XZ grid, area weights normalized independently per N64 poly.

    Tiny polygons with no grid hit get a centroid sample so they are not omitted.
    This approximates polygon area coverage; it is not a union of floor layers.
    """
    points, weights = [], []
    for tri in triangles:
        normal = np.cross(tri[1]-tri[0], tri[2]-tri[0])
        area = np.linalg.norm(normal)*.5
        if area < 1e-8:
            continue
        xz = tri[:, [0, 2]]
        lo = np.ceil(xz.min(0)/spacing-.5).astype(int)
        hi = np.floor(xz.max(0)/spacing-.5).astype(int)
        if (hi >= lo).all():
            xx, zz = np.meshgrid((np.arange(lo[0], hi[0]+1)+.5)*spacing,
                                 (np.arange(lo[1], hi[1]+1)+.5)*spacing)
            xzpoints = np.stack((xx.ravel(), zz.ravel()), axis=1)
            a, b, c = xz
            den = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
            u = ((xzpoints[:, 0]-a[0])*(c[1]-a[1])-(xzpoints[:, 1]-a[1])*(c[0]-a[0]))/den
            v = ((b[0]-a[0])*(xzpoints[:, 1]-a[1])-(b[1]-a[1])*(xzpoints[:, 0]-a[0]))/den
            keep = (u >= -1e-9) & (v >= -1e-9) & (u+v <= 1+1e-9)
            xzpoints = xzpoints[keep]
            sample = tri[0]+u[keep, None]*(tri[1]-tri[0])+v[keep, None]*(tri[2]-tri[0])
        else:
            sample = np.empty((0, 3))
        if not len(sample):
            sample = tri.mean(0)[None]
        points.append(sample)
        weights.append(np.full(len(sample), area/len(sample)))
    return (np.concatenate(points), np.concatenate(weights)) if points else (np.empty((0, 3)), np.empty(0))


def spatial_index(triangles, cell=250.):
    cells = defaultdict(list)
    for i, tri in enumerate(triangles):
        lo = np.floor(tri[:, [0, 2]].min(0)/cell).astype(int)
        hi = np.floor(tri[:, [0, 2]].max(0)/cell).astype(int)
        for x in range(lo[0], hi[0]+1):
            for z in range(lo[1], hi[1]+1):
                cells[x, z].append(i)
    return cells


def vertical_distances(points, triangles):
    distances = np.full(len(points), np.inf)
    signed = np.full(len(points), np.nan)
    below = np.full(len(points), np.inf)
    above = np.full(len(points), np.inf)
    grid = spatial_index(triangles)
    groups = defaultdict(list)
    for i, key in enumerate(np.floor(points[:, [0, 2]]/250.).astype(int)):
        groups[tuple(key)].append(i)
    for key, ids in groups.items():
        candidates = grid.get(key, [])
        if not candidates:
            continue
        tri = triangles[candidates]
        a, b, c = tri[:, 0], tri[:, 1], tri[:, 2]
        e1, e2 = b-a, c-a
        den = e1[:, 0]*e2[:, 2]-e1[:, 2]*e2[:, 0]
        valid = abs(den) > 1e-8
        a, e1, e2, den = a[valid], e1[valid], e2[valid], den[valid]
        if not len(den):
            continue
        batch = max(1, 1000000//len(den))
        for start in range(0, len(ids), batch):
            ii = np.array(ids[start:start+batch])
            p = points[ii]
            dx, dz = p[:, None, 0]-a[None, :, 0], p[:, None, 2]-a[None, :, 2]
            u = (dx*e2[None, :, 2]-dz*e2[None, :, 0])/den
            v = (e1[None, :, 0]*dz-e1[None, :, 2]*dx)/den
            hit = (u >= -1e-7) & (v >= -1e-7) & (u+v <= 1+1e-7)
            delta = a[None, :, 1]+u*e1[None, :, 1]+v*e2[None, :, 1]-p[:, None, 1]
            absd = np.where(hit, abs(delta), np.inf)
            nearest = absd.argmin(1)
            distances[ii] = absd[np.arange(len(ii)), nearest]
            signed[ii] = np.where(np.isfinite(distances[ii]), delta[np.arange(len(ii)), nearest], np.nan)
            above[ii] = np.where(hit & (delta >= -1e-6), np.maximum(delta, 0), np.inf).min(1)
            below[ii] = np.where(hit & (delta <= 1e-6), np.maximum(-delta, 0), np.inf).min(1)
    return distances, signed, below, above


def wall_distances(ntri, normals, mtri, limit=250.):
    """Area-weighted centroid rays along N64 wall normals, censored at 250 units.

    Bidirectional line/triangle intersections, not an exhaustive nearest-wall metric.
    The XZ index plus 3x3 neighboring cells includes all hits within the limit.
    """
    grid = spatial_index(mtri)
    centers = ntri.mean(1)
    areas = np.linalg.norm(np.cross(ntri[:, 1]-ntri[:, 0], ntri[:, 2]-ntri[:, 0]), axis=1)*.5
    distances = np.full(len(ntri), np.inf)
    for i, (p, direction) in enumerate(zip(centers, normals)):
        key = np.floor(p[[0, 2]]/250.).astype(int)
        ids = sorted({j for x in range(key[0]-1, key[0]+2) for z in range(key[1]-1, key[1]+2)
                      for j in grid.get((x, z), [])})
        if not ids:
            continue
        tri = mtri[ids]
        a, e1, e2 = tri[:, 0], tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0]
        normal = np.cross(e1, e2)
        den = normal @ direction
        good = abs(den) > 1e-8
        t = np.divide(np.sum((a-p)*normal, axis=1), den, out=np.full(len(ids), np.inf), where=good)
        good &= abs(t) <= limit
        if not good.any():
            continue
        a, e1, e2, t = a[good], e1[good], e2[good], t[good]
        q = p+t[:, None]*direction-a
        d00, d01, d11 = (np.sum(e1*e1, axis=1), np.sum(e1*e2, axis=1), np.sum(e2*e2, axis=1))
        d20, d21 = np.sum(q*e1, axis=1), np.sum(q*e2, axis=1)
        denominator = d00*d11-d01*d01
        good = abs(denominator) > 1e-8
        u = np.divide(d11*d20-d01*d21, denominator, out=np.full(len(t), np.inf), where=good)
        v = np.divide(d00*d21-d01*d20, denominator, out=np.full(len(t), np.inf), where=good)
        inside = (u >= -1e-6) & (v >= -1e-6) & (u+v <= 1+1e-6)
        if inside.any():
            distances[i] = np.min(abs(t[inside]))
    return distances, areas


def weighted_percentile(values, weights, quantile):
    finite = np.isfinite(values)
    if not finite.any():
        return None
    values, weights = values[finite], weights[finite]
    order = np.argsort(values)
    at = np.searchsorted(np.cumsum(weights[order]), weights.sum()*quantile)
    return float(values[order[min(at, len(order)-1)]])


def coverage(distances, weights):
    total = weights.sum()
    return {str(t): float(weights[distances <= t+1e-6].sum()/total) if total else None
            for t in (2, 5, 10, 25)}


def coordinate_probe(n64, mm):
    nset = set(map(tuple, n64['vertices'].astype(int)))
    mset = set(map(tuple, mm['vertices'].astype(int)))
    scales = (.01, .5, 1., 2., 10., 100.)
    best, candidates = -1, []
    for axes in itertools.permutations(range(3)):
        for signs in itertools.product((-1, 1), repeat=3):
            signed_axes = n64['vertices'][:, axes]*signs
            for scale in scales:
                transformed = set(map(tuple, signed_axes*scale))
                shared = len(transformed & mset)
                if shared > best:
                    best, candidates = shared, []
                if shared == best:
                    candidates.append(dict(axes=list(axes), signs=list(signs), scale=scale))
    return dict(exact_shared_vertices=len(nset & mset), n64_unique_vertices=len(nset),
                best_transform_shared_vertices=best, best_transforms=candidates,
                n64_bounds=np.array([n64['vertices'].min(0), n64['vertices'].max(0)]).tolist(),
                mm3d_bounds=np.array([mm['vertices'].min(0), mm['vertices'].max(0)]).tolist())


def study(romfs, o2r, out, table, spacing):
    if not math.isfinite(spacing) or spacing <= 0:
        raise ValueError('grid spacing must be finite and positive')
    results, failures, samples_by_scene = [], [], {}
    with zipfile.ZipFile(o2r) as archive:
        for row in table:
            name, segment = row['mm3d_scene'], row['segment']
            try:
                matches = [n for n in archive.namelist() if n.startswith(f'scenes/nonmq/{segment}/{segment}CollisionHeader_') and not n.endswith('.meta')]
                if len(matches) != 1:
                    raise FormatError(f'expected one N64 collision resource, got {len(matches)}')
                n64 = read_n64(archive.read(matches[0]))
                setups = scene.parse((romfs/'scenes'/f'{name}_info.zsi').read_bytes())
                collisions = [s['collision'] for s in scene.setups(setups) if 'collision' in s]
                if not collisions:
                    raise FormatError('no native MM3D collision (render proxy not evaluated)')
                mm = collisions[0]
                if any(not np.array_equal(mm['vertices'], c['vertices']) or not np.array_equal(mm['indices'], c['indices']) for c in collisions):
                    raise FormatError('alternate collision variants require separate comparison')
                ntri, mtri = n64['vertices'][n64['indices']], mm['vertices'][mm['indices']]
                floors = ntri[n64['normals'][:, 1] > .5]
                points, weights = floor_samples(floors, spacing)
                dist, signed, below, above = vertical_distances(points, mtri)
                walls = np.abs(n64['normals'][:, 1]) <= .5
                mm_walls = np.abs(mm['normals'][:, 1]) <= .5
                wd, wa = wall_distances(ntri[walls], n64['normals'][walls], mtri[mm_walls])
                cover = coverage(dist, weights)
                missing = float(weights[~np.isfinite(dist)].sum()/weights.sum()) if weights.sum() else None
                # Conservative triage categories are study labels, not gameplay approval.
                if cover['5'] is None:
                    verdict = 'no sampled floors'
                elif cover['5'] >= .99 and cover['25'] >= .999:
                    verdict = 'candidate; inspect seams/walls'
                elif cover['10'] >= .95 and cover['25'] >= .99:
                    verdict = 'needs local visual adaptation'
                else:
                    verdict = 'unsafe wholesale overlay'
                result = dict(**row, samples=len(points), floor_polygons=len(floors), floor_area=float(weights.sum()),
                    within=cover, missing_xz=missing, p50=weighted_percentile(dist, weights, .5),
                    p95=weighted_percentile(dist, weights, .95),
                    above_p95=weighted_percentile(above, weights, .95), below_p95=weighted_percentile(below, weights, .95),
                    wall_polygons=int(walls.sum()), wall_within=coverage(wd, wa),
                    wall_p95=weighted_percentile(wd, wa, .95), coordinate=coordinate_probe(n64, mm), verdict=verdict)
                results.append(result)
                samples_by_scene[name] = dict(xyz=points, area_weights=weights, distance=dist,
                                               signed=signed, below=below, above=above)
                (out/name).mkdir(parents=True, exist_ok=True)
                np.savez_compressed(out/name/'collision-samples.npz', **samples_by_scene.pop(name))
                print('collision', name, 'samples', len(points), 'within5', cover['5'], verdict, flush=True)
            except Exception as exc:
                failures.append(dict(scene=name, error=f'{type(exc).__name__}: {exc}'))
                print('collision failure', name, str(exc), flush=True)
    results.sort(key=lambda r: (-(r['within']['5'] or 0), -(r['within']['25'] or 0), r['p95'] or 0))
    unpaired = sorted(p.name[:-9] for p in (romfs/'scenes').glob('*_info.zsi')
                      if not re.search(r'_\d+_info\.zsi$', p.name) and p.name[:-9] not in {r['mm3d_scene'] for r in table})
    report = dict(unpaired_mm3d_scenes=unpaired, grid_spacing=spacing, source='native MM3D ZSI collision', scenes=results, failures=failures)
    out.mkdir(parents=True, exist_ok=True)
    (out/'collision-report.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    def pct(v):
        return 'n/a' if v is None else f'{v*100:.2f}'
    def units(v):
        return 'n/a' if v is None else f'{v:.2f}'
    lines = ['# N64 / MM3D static collision comparison', '',
        f'{len(results)} paired scenes; {len(failures)} failures. World XZ grid spacing {spacing:g} units.', '',
        'Reference: walkable N64 polys have stored normal Y > 0.5. Each polygon is sampled on a world-aligned XZ grid; tiny polygons use a centroid. Samples share the polygon\'s actual 3D area equally. Overlapping layers remain separate. The nearest MM3D triangle at the same XZ is searched in both vertical directions, including ceilings; this can overestimate traversable agreement. Missing XZ hits count as outside every tolerance. p50/p95 are area-weighted over finite hits only. Per-sample signed, above and below distances are in each scene\'s collision-samples.npz.', '',
        'Wall coverage uses one area-weighted centroid ray per N64 wall along its stored normal, both directions, with a 250-unit limit. This is a local correspondence diagnostic, not an exhaustive nearest-wall or actor-clearance test. Floors and walls compare static collision only; dynamic actors, triggers, transitions, render-only decoration, animation and camera obstruction are untested.', '',
        'Coordinates: shared XYZ, +Y up, scale 1, same handedness and origin; no reflection or offset applied. Exact shared vertices and both bounds are recorded per scene, together with an exhaustive 48 signed axis permutations × scales 0.01/0.5/1/2/10/100 anchor search. Symmetric arenas can tie under other transforms; changed layouts do not justify mirroring.', '',
        'Verdicts are conservative triage: candidate requires >=99% within5 and >=99.9% within25; local adaptation requires >=95% within10 and >=99% within25; others are unsafe for wholesale overlay. Even candidate scenes require visual-to-collision and wall/seam review before enabling. Preserving N64 collision remains mandatory.', '',
        '|Scene ID|Scene|≤2 %|≤5 %|≤10 %|≤25 %|Missing XZ %|p50|p95|Walls ≤5 %|Verdict|',
        '|--:|---|--:|--:|--:|--:|--:|--:|--:|--:|---|']
    for r in results:
        lines.append(f"|0x{r['scene_id']:02X}|{r['mm3d_scene']}|"+'|'.join(pct(r['within'][str(t)]) for t in (2, 5, 10, 25))+
                     f"|{pct(r['missing_xz'])}|{units(r['p50'])}|{units(r['p95'])}|{pct(r['wall_within']['5'])}|{r['verdict']}|")
    lines += ['', '## Unpaired MM3D scenes', '', ', '.join(unpaired)+' — no N64 scene/collision comparator.', '', '## Verdict counts', '']
    for verdict in sorted({r['verdict'] for r in results}):
        lines.append(f"- {verdict}: {sum(r['verdict'] == verdict for r in results)}")
    lines += ['', '## Failures', '', json.dumps(failures, indent=2)]
    (out/'collision-report.md').write_text('\n'.join(lines)+'\n')
    return failures
