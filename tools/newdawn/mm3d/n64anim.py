"""Read the N64 scene's animated materials (TexAnim) and the room triangles each animated segment touches.

Everything is read from the local mm.o2r; nothing is written next to the game data.
Resource layouts follow the 2 Ship exporter (NATO = texture animation, TLDO = display list).
"""
import re
import struct
import zipfile
from pathlib import Path

DEFAULT_O2R = Path.home()/'dev/2ship2harkinian/build-cmake/mm/mm.o2r'
SCROLL_TYPES = (0, 1)  # single / two layer texture scroll: the only types mapped onto MM3D materials
_HASH_OPS = {0x20, 0x31, 0x32, 0x35, 0x33, 0x36, 0x42}  # opcodes followed by a 64-bit resource hash


def _crc64(name):
    v, poly, mask = (1 << 64) - 1, 0x42f0e1eba9ea3693, (1 << 64) - 1
    for b in name.encode():
        v ^= b << 56
        for _ in range(8):
            v = ((v << 1) ^ poly if v & (1 << 63) else v << 1) & mask
    return v


def parse_texanim(d):
    """-> [{segment, type, params}] (segment is the absolute N64 segment number, 8..13)."""
    p, out = 68, []
    for _ in range(struct.unpack_from('<I', d, 64)[0]):
        s, t = struct.unpack_from('<bh', d, p)
        p += 3
        e = {'segment': abs(s) + 7, 'type': t}
        if t in (0, 1):  # (xStep, yStep, width, height) per layer
            e['params'] = [struct.unpack_from('<bbBB', d, p + 4 * j) for j in range(t + 1)]
            p += 4 * (t + 1)
        elif t in (2, 3, 4):
            _, _, nk = struct.unpack_from('<HHH', d, p)
            p += 6 + 2 * nk
            n = struct.unpack_from('<H', d, p)[0]
            p += 2 + 5 * n
            n = struct.unpack_from('<H', d, p)[0]
            p += 2 + 4 * n
        elif t == 5:
            length, count = struct.unpack_from('<HI', d, p)
            p += 6
            for _ in range(count):
                p += 4 + struct.unpack_from('<I', d, p)[0]
            p += length
        else:
            p += 4
        out.append(e)
    return out


def scene_animations(z, scene):
    """Non-trivial animated materials of an N64 scene (upper-case resource name) -> {segment: entry}."""
    found = {}
    for n in z.namelist():
        if re.fullmatch(r'scenes/nonmq/%s/\w+TexAnim_[0-9A-Fa-f]+' % scene, n):
            d = z.read(n)
            if d[4:8] == b'NATO':
                for e in parse_texanim(d):
                    if e['type'] != 6:  # 6 = no-op
                        found[e['segment']] = e
    return found


def animated_scenes(o2r=DEFAULT_O2R):
    with zipfile.ZipFile(o2r) as z:
        names = {m.group(1) for n in z.namelist() if (m := re.match(r'scenes/nonmq/(\w+)/', n))}
        return {s for s in names if scene_animations(z, s)}


def animated_triangles(z, scene):
    """-> {segment: float32 array (n, 3, 3)} world-space N64 room triangles drawn after a segment DL call."""
    import numpy as np
    byhash = {_crc64(n): n for n in z.namelist()}
    result = {}
    for name in z.namelist():
        if not (name.startswith('scenes/nonmq/%s/' % scene) and '_room_' in name and 'DL_' in name):
            continue
        raw = z.read(name)
        if raw[4:8] != b'TLDO':
            continue
        cmds, p = [], 72
        while p + 8 <= len(raw):
            a, b = struct.unpack_from('<II', raw, p)
            p += 8
            extra = None
            if a >> 24 in _HASH_OPS:
                hi, lo = struct.unpack_from('<II', raw, p)
                extra = byhash.get((hi << 32) | lo)
                p += 8
            cmds.append((a >> 24, a, b, extra))
        segment, loaded = None, {}
        for op, a, b, extra in cmds:
            if op == 0xde and 8 <= (b >> 24) <= 13:
                segment = b >> 24
            elif op == 0x32:  # vertex load by resource
                count, start = (a >> 12) & 255, ((a >> 1) & 127) - ((a >> 12) & 255)
                if extra is None:
                    raise ValueError('unresolved vertex resource in ' + name)
                verts = np.frombuffer(z.read(extra), dtype='<i2', offset=72).reshape(-1, 8)[:, :3]
                for i, v in enumerate(verts[b // 16:b // 16 + count]):
                    loaded[start + i] = v
            elif op in (5, 6) and segment is not None:
                tris = [[(a >> s) & 255 for s in (16, 8, 0)]]
                if op == 6:
                    tris.append([(b >> s) & 255 for s in (16, 8, 0)])
                result.setdefault(segment, []).extend([[loaded[k // 2].astype('f4') for k in t] for t in tris])
    return {s: np.array(t, dtype='f4') for s, t in result.items()}
