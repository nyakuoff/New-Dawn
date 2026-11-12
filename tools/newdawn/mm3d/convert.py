#!/usr/bin/env python3
"""Offline MM3D world converter. Inputs and generated output stay outside git."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import traceback
import numpy as np

from binary import FormatError, Reader, gar, inflate, literal_lzs
import cmb
import scene
from texture import ctxb, decode, etc_block, textures
from preview import render

DEFAULT_ROMFS = Path('/home/loganp/dev/ghm-mods/mm3d/romfs')
DEFAULT_OUT = Path('/home/loganp/dev/ghm-mods/mm3d/out')
DEFAULT_O2R = Path('/home/loganp/dev/2ship2harkinian/build-cmake/mm/mm.o2r')
REPO = Path(__file__).resolve().parents[3]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')


def scene_table(header):
    """Derive IDs from the fork's table, including holes; never renumber entries."""
    rows = []
    for line in header.read_text().splitlines():
        match = re.match(r'/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*DEFINE_SCENE\((\w+),\s*(\w+),', line)
        if match:
            sid, segment, enum = match.groups()
            title = re.search(r'"([^"\n]*)"\)\s*$', line)
            rows.append(dict(scene_id=int(sid, 16), enum=enum, segment=segment,
                             mm3d_scene=segment.lower(), name=title[1] if title else segment))
    if not rows or len({x['scene_id'] for x in rows}) != len(rows):
        raise FormatError('invalid/empty scene table')
    return rows


def export_texture(t, out):
    meta = {k: v for k, v in t.items() if k != 'levels'}
    meta['faces'] = []
    for face in t['levels']:
        levels = []
        for rgba in face:
            data = rgba.tobytes()
            digest = hashlib.sha256(struct.pack('<II', rgba.shape[1], rgba.shape[0])+data).hexdigest()
            name = f'textures/{digest}.rgba'
            p = out/name
            if not p.exists():
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
            levels.append(dict(path=name, width=rgba.shape[1], height=rgba.shape[0]))
        meta['faces'].append(levels)
    return meta


def export_mesh(mesh, path, out):
    meta = {k: mesh[k] for k in ('name', 'draws', 'materials', 'bones', 'sections')}
    meta['textures'] = [export_texture(t, out) for t in mesh['textures']]
    vbo, ibo = mesh['vertices'], mesh['indices']
    meta['bounds'] = [vbo[:, :3].min(0).tolist(), vbo[:, :3].max(0).tolist()] if len(vbo) else None
    header = json.dumps(meta, separators=(',', ':'), allow_nan=False).encode()
    header += b' '*(-len(header) % 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as f:
        f.write(struct.pack('<8s4I', b'NDMM3D01', len(header), len(vbo), len(ibo), 48))
        f.write(header)
        f.write(vbo.astype('<f4').tobytes())
        f.write(ibo.astype('<u4').tobytes())
    # Read back the exported buffers, not merely the input parser's arrays.
    raw = path.read_bytes()
    magic, nj, nv, ni, stride = struct.unpack_from('<8s4I', raw)
    if magic != b'NDMM3D01' or stride != 48 or len(raw) != 24+nj+nv*48+ni*4:
        raise FormatError('exported mesh length/header mismatch')
    reread = np.frombuffer(raw, '<f4', count=nv*12, offset=24+nj).reshape(-1, 12)
    indices = np.frombuffer(raw, '<u4', count=ni, offset=24+nj+nv*48)
    if not np.isfinite(reread).all() or (ni and indices.max() >= nv):
        raise FormatError('exported mesh structural check failed')
    return dict(path=str(path.relative_to(out)), vertices=len(vbo), triangles=len(ibo)//3,
                draws=len(mesh['draws']), bounds=meta['bounds'])


def metadata(setup):
    return {k: setup[k] for k in ('header', 'commands', 'rooms', 'environment', 'setup_refs')}


def convert_scene(name, romfs, out, preview=False):
    source = romfs/'scenes'/f'{name}_info.zsi'
    parsed = scene.parse(source.read_bytes())
    manifest = dict(format='NDMM3D01', scene=name, source=str(source.relative_to(romfs)),
                    setups=[], rooms=[], collision_source='native ZSI command 0x03')
    room_names = []
    for setup in scene.setups(parsed):
        manifest['setups'].append(metadata(setup))
        for ref in setup['rooms']:
            basename = Path(ref).name
            if basename not in room_names:
                room_names.append(basename)
    seen = {}
    preview_meshes = []
    totals = Counter()
    for room_name in room_names:
        room = scene.parse((romfs/'scenes'/room_name).read_bytes())
        room_meta = dict(source=room_name, setups=[])
        first_nonempty = True
        for setup_index, setup in enumerate(scene.setups(room)):
            smeta = metadata(setup)
            smeta['meshes'] = []
            for raw in setup['meshes']:
                key = hashlib.sha256(raw['data']).hexdigest()
                path = out/name/f'{key}.ndmesh'
                mesh = None
                if key not in seen or (preview and first_nonempty):
                    mesh = cmb.parse(raw['data'])
                if key not in seen:
                    entry = export_mesh(mesh, path, out)
                    seen[key] = entry
                    totals.update(meshes=1, vertices=entry['vertices'], triangles=entry['triangles'],
                                  draws=entry['draws'], textures=len(mesh['textures']))
                smeta['meshes'].append(dict(part=raw['part'], entry=raw['entry'], **seen[key]))
                if preview and first_nonempty:
                    preview_meshes.append(mesh)
            if setup['meshes']:
                first_nonempty = False
            room_meta['setups'].append(smeta)
        manifest['rooms'].append(room_meta)
    totals['rooms'] = len(room_names)
    manifest['counts'] = dict(totals)
    write_json(out/name/'manifest.json', manifest)
    if preview:
        render(preview_meshes, out/'preview'/f'{name}-oblique.png')
        render(preview_meshes, out/'preview'/f'{name}-top.png', top=True)
    return dict(scene=name, **totals)


def audit(romfs, out):
    """Inflate every file; inspect every GAR; decode every CTXB/CMB texture mip."""
    counts, formats, failures = Counter(), Counter(), []
    seen_textures = set()

    def inspect(data, origin, embedded=False):
        try:
            if data[:4] == b'LzS\x01':
                counts['lzs'] += 1
            data = inflate(data)
            magic = data[:4]
            if magic == b'GAR\x02':
                counts['gar'] += 1
                for name, raw in gar(data).items():
                    inspect(raw, origin+'!'+name)
            elif magic == b'ctxb' or magic == b'cmb ':
                counts['ctxb' if magic == b'ctxb' else ('embedded_room_cmb' if embedded else 'cmb')] += 1
                digest = hashlib.sha256(data).digest()
                if digest in seen_textures:
                    return
                seen_textures.add(digest)
                if magic == b'ctxb':
                    tex = ctxb(data)
                else:
                    r = Reader(data)
                    if r.u32(8) != 10:
                        raise FormatError('audit encountered non-v10 CMB')
                    tx, td = r.u32(0x30), r.u32(0x44)
                    tex = textures(r.data[tx:], r.data[td:] if td else None)
                for t in tex:
                    formats[t['format']] += 1
                    counts['texture_entries'] += 1
                    counts['decoded_mips'] += sum(len(face) for face in t['levels'])
                    if not t['levels']:
                        counts['external_texture_references'] += 1
            elif magic == b'ZSI\x09':
                counts['zsi'] += 1
                collision_only = '!' in origin and data[16:17] == b'\x03'
                counts['collision_only_zsi'] += int(collision_only)
                parsed = scene.parse(data, collision_only=collision_only)
                counts['zsi_setups'] += len(list(scene.setups(parsed)))
                for setup in scene.setups(parsed):
                    for mesh in setup['meshes']:
                        inspect(mesh['data'], origin+f"!setup{setup['header']}:{mesh['part']}", embedded=True)
            else:
                counts['other'] += 1
        except Exception as exc:
            failures.append(dict(source=origin, error=f'{type(exc).__name__}: {exc}'))
    for path in sorted(romfs.rglob('*')):
        if path.is_file():
            counts['files'] += 1
            inspect(path.read_bytes(), str(path.relative_to(romfs)))
    result = dict(counts=dict(counts), texture_formats=dict(formats), failures=failures)
    write_json(out/'audit.json', result)
    print('audit:', json.dumps(result), flush=True)
    return result


def self_check(romfs):
    raw = bytes(range(256))*3+b'new dawn'
    if inflate(literal_lzs(raw)) != raw:
        raise AssertionError('literal LzS roundtrip')
    # literal ABC, then an overlapping ring match -> ABCABCABCABC.
    payload = b'\x07ABC\xee\xf6'
    packed = b'LzS\x01'+struct.pack('<III', 0, 12, len(payload))+payload
    if inflate(packed) != b'ABCABCABCABC':
        raise AssertionError('overlapping LzS match')
    block = etc_block(0, 0)
    if not np.all(block == 2):
        raise AssertionError('ETC1 black individual block + modifier 2')
    # Individual red block with the negative-small selector (-2).
    b = etc_block(0xff000000, 0xffff0000)
    if not np.all(b[:, :, 0] == 253) or not np.all(b[:, :, 1:] == 0):
        raise AssertionError('ETC1 selector sign')
    rgb565 = decode('RGB565', 8, 8, struct.pack('<H', 4 << 11)*64)
    if not np.all(rgb565[:, :, 0] == 33):
        raise AssertionError('PICA 5-bit replication')
    alpha = decode('ETC1A4', 8, 8, (bytes.fromhex('1032547698badcfe')+bytes(8))*4)
    if alpha[0, 0, 3] != 0 or alpha[3, 3, 3] != 255 or alpha[1, 0, 3] != 17:
        raise AssertionError('ETC1A4 alpha column order')
    from collision import floor_samples, vertical_distances, wall_distances
    triangle = np.array([[[0., 5., 0.], [1., 5., 0.], [0., 5., 1.]]])
    dist, signed, below, above = vertical_distances(np.array([[.2, 2., .2], [2., 2., 2.]]), triangle)
    if not (dist[0] == signed[0] == above[0] == 3 and np.isinf(below[0]) and np.isinf(dist[1])):
        raise AssertionError('analytic vertical intersection/missing XZ')
    _, weights = floor_samples(triangle, .25)
    if not np.isclose(weights.sum(), .5):
        raise AssertionError('floor sample area conservation')
    wall = np.array([[[0., 0., 0.], [0., 10., 0.], [0., 0., 10.]]])
    distances, _ = wall_distances(wall+np.array([3., 0., 0.]), np.array([[1., 0., 0.]]), wall)
    if not np.isclose(distances[0], 3):
        raise AssertionError('analytic wall ray')
    room = scene.parse((romfs/'scenes/z2_insidetower_0_info.zsi').read_bytes())
    meshes = [cmb.parse(m['data']) for s in scene.setups(room) for m in s['meshes']]
    if not meshes or not all(len(m['vertices']) and len(m['indices']) for m in meshes):
        raise AssertionError('real room parse is empty')
    print(f'self-check PASS: LzS literal/match, ETC1/ETC1A4, RGB565, analytic collision, real room ({len(meshes)} setup meshes)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, default=DEFAULT_ROMFS)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--o2r', type=Path, default=DEFAULT_O2R)
    parser.add_argument('--scene', help='MM3D scene name, e.g. z2_clocktower')
    parser.add_argument('--all', action='store_true', help='convert all 111 scene files')
    parser.add_argument('--audit', action='store_true')
    parser.add_argument('--collision', action='store_true')
    parser.add_argument('--grid', type=float, default=25., help='collision sampling grid spacing in game units')
    parser.add_argument('--self-check', action='store_true')
    parser.add_argument('--generate-table', action='store_true')
    args = parser.parse_args()
    args.out = args.out.resolve()
    if args.out.is_relative_to(REPO) or args.out.is_relative_to(args.romfs.resolve()):
        parser.error('outputs must be outside repository and separate from RomFS')
    if args.scene and not re.fullmatch(r'[a-z0-9_]+', args.scene):
        parser.error('scene must be a lower-case MM3D scene name')
    table = scene_table(REPO/'mm/include/tables/scene_table.h')
    if args.generate_table:
        write_json(Path(__file__).with_name('scene-table.json'), table)
        print(f'generated {len(table)} scene mappings')
    if args.self_check:
        self_check(args.romfs)
    failures = []
    if args.all or args.scene:
        names = sorted(p.name[:-9] for p in (args.romfs/'scenes').glob('*_info.zsi')
                       if not re.search(r'_\d+_info\.zsi$', p.name)) if args.all else [args.scene]
        results = []
        for name in names:
            try:
                result = convert_scene(name, args.romfs, args.out,
                    preview=name in ('z2_clocktower', 'z2_00keikoku', 'z2_insidetower'))
                results.append(result)
                print(json.dumps(result), flush=True)
            except Exception as exc:
                failures.append(dict(scene=name, error=f'{type(exc).__name__}: {exc}'))
                traceback.print_exc()
        counts = {k: sum(r.get(k, 0) for r in results) for k in ('rooms', 'meshes', 'vertices', 'triangles', 'draws', 'textures')}
        write_json(args.out/'conversion-report.json', dict(converted=len(results), attempted=len(names),
                   totals=counts, scenes=results, failures=failures))
        print('conversion totals:', len(results), counts, 'failures:', failures, flush=True)
    if args.audit:
        failures.extend(audit(args.romfs, args.out)['failures'])
    if args.collision:
        from collision import study
        failures.extend(study(args.romfs, args.o2r, args.out, table, args.grid))
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
