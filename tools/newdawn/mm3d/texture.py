"""PICA 8x8 Morton tiles and ETC1/ETC1A4 to RGBA8, independently implemented."""
import struct
import numpy as np
from binary import Reader, FormatError, inflate

FORMATS = {0x675A: 'ETC1', 0x675B: 'ETC1A4', 0x14016754: 'RGB8',
           0x14016752: 'RGBA8', 0x80336752: 'RGBA4444', 0x80346752: 'RGBA5551',
           0x83636754: 'RGB565', 0x14016756: 'A8', 0x67616756: 'A4',
           0x14016757: 'L8', 0x67616757: 'L4', 0x14016758: 'LA8',
           0x67606758: 'LA4', 0x14016759: 'HILO8'}
BPP = dict(RGBA8=32, RGB8=24, RGBA4444=16, RGBA5551=16, RGB565=16,
           HILO8=16, LA8=16, A8=8, L8=8, LA4=8, L4=4, A4=4, ETC1=4, ETC1A4=8)
MODIFIERS = np.array([[2, 8, -2, -8], [5, 17, -5, -17], [9, 29, -9, -29],
                      [13, 42, -13, -42], [18, 60, -18, -60],
                      [24, 80, -24, -80], [33, 106, -33, -106],
                      [47, 183, -47, -183]], dtype=np.int32)


def etc_block(control, selectors):
    """Return a 4x4 RGB block; words are PICA little-endian low/high u32."""
    bases = []
    if control & 2:
        for shift in (27, 19, 11):
            base = (control >> shift) & 31
            delta = (control >> (shift - 3)) & 7
            delta = delta - 8 if delta & 4 else delta
            other = base + delta
            if not 0 <= other <= 31:
                raise FormatError('ETC1 invalid differential colour')
            bases.append([(base << 3) | (base >> 2), (other << 3) | (other >> 2)])
    else:
        for shift in (28, 20, 12):
            bases.append([((control >> shift) & 15) * 17,
                          ((control >> (shift - 4)) & 15) * 17])
    bases = np.array(bases).T
    y, x = np.indices((4, 4))
    k = x * 4 + y
    sel = ((selectors >> k) & 1) | (((selectors >> (k + 16)) & 1) << 1)
    half = (y if control & 1 else x) // 2
    table = np.where(half == 0, (control >> 5) & 7, (control >> 2) & 7)
    return np.clip(bases[half] + MODIFIERS[table, sel, None], 0, 255).astype(np.uint8)


def byte_size(fmt, w, h):
    # Mips smaller than 8x8 still occupy a complete PICA tile.
    return ((w + 7) // 8) * ((h + 7) // 8) * 64 * BPP[fmt] // 8


def decode(fmt, width, height, data):
    if not (0 < width <= 8192 and 0 < height <= 8192):
        raise FormatError('invalid texture dimensions')
    n = byte_size(fmt, width, height)
    r = Reader(data)
    r.take(0, n)
    pw, ph = (width + 7) // 8 * 8, (height + 7) // 8 * 8
    out = np.full((ph, pw, 4), 255, dtype=np.uint8)
    if fmt.startswith('ETC1'):
        off = 0
        for yy in range(0, ph, 8):
            for xx in range(0, pw, 8):
                for by, bx in ((0, 0), (0, 4), (4, 0), (4, 4)):
                    alpha = None
                    if fmt == 'ETC1A4':
                        alpha = r.unpack('<Q', off)[0]
                        off += 8
                    selectors, control = r.unpack('<II', off)
                    off += 8
                    out[yy+by:yy+by+4, xx+bx:xx+bx+4, :3] = etc_block(control, selectors)
                    if alpha is not None:
                        for x in range(4):
                            for y in range(4):
                                out[yy+by+y, xx+bx+x, 3] = ((alpha >> (4*(4*x+y))) & 15)*17
    else:
        count = pw * ph
        src = np.frombuffer(r.take(0, n), dtype=np.uint8)
        values = np.full((count, 4), 255, dtype=np.uint8)
        if fmt == 'RGBA8':
            values[:] = src.reshape(-1, 4)[:, ::-1]
        elif fmt == 'RGB8':
            values[:, :3] = src.reshape(-1, 3)[:, ::-1]
        elif fmt in ('RGBA4444', 'RGBA5551', 'RGB565'):
            v = np.frombuffer(r.take(0, n), dtype='<u2').astype(np.uint32)
            bits = {'RGBA4444': (4, 4, 4, 4), 'RGBA5551': (5, 5, 5, 1), 'RGB565': (5, 6, 5)}[fmt]
            shift = 16
            for c, b in enumerate(bits):
                shift -= b
                raw = (v >> shift) & ((1 << b)-1)
                expanded = ((raw << 3) | (raw >> 2)) if b == 5 else (((raw << 2) | (raw >> 4)) if b == 6 else raw * (17 if b == 4 else 255))
                values[:, c] = expanded.astype(np.uint8)
        elif fmt in ('LA8', 'HILO8'):
            v = src.reshape(-1, 2)
            if fmt == 'LA8':
                values[:, :3] = v[:, 1, None]
                values[:, 3] = v[:, 0]
            else:
                values[:, 0], values[:, 1] = v[:, 1], v[:, 0]
        else:
            if fmt in ('L4', 'A4'):
                v = np.stack((src & 15, src >> 4), axis=1).reshape(-1)*17
            else:
                v = src
            if fmt.startswith('L'):
                lum = (v >> 4)*17 if fmt == 'LA4' else v
                values[:, :3] = lum[:, None]
            if fmt.startswith('A'):
                values[:, 3] = v
            elif fmt == 'LA4':
                values[:, 3] = (v & 15)*17
            elif fmt in ('L4', 'L8'):
                values[:, 3] = v  # PICA luminance is replicated to alpha too.
        i = np.arange(count)
        local = i % 64
        x = (local & 1) | ((local >> 1) & 2) | ((local >> 2) & 4)
        y = ((local >> 1) & 1) | ((local >> 2) & 2) | ((local >> 3) & 4)
        tile = i // 64
        out[(tile // (pw//8))*8+y, (tile % (pw//8))*8+x] = values
    return out[:height, :width].copy()


def textures(chunk, payload=None):
    r = Reader(chunk)
    r.magic(0, b'tex ')
    result = []
    for i in range(r.u32(8)):
        off = 12 + i * 36
        size, levels, etc, cube, w, h, glfmt, start = r.unpack('<IHBBHHII', off)
        if glfmt not in FORMATS:
            raise FormatError(f'unknown texture format {glfmt:#x}')
        t = dict(name=r.string(off+20, 16), width=w, height=h, format=FORMATS[glfmt],
                 mip_count=levels, cubemap=bool(cube), levels=[])
        if payload is not None:
            if cube:
                starts = r.unpack('<6I', 12 + r.u32(8)*36 + start)
            else:
                starts = [start]
            for face_start in starts:
                face = []
                pr = Reader(payload)
                consumed = 0
                for mip in range(levels):
                    mw, mh = max(1, w >> mip), max(1, h >> mip)
                    n = byte_size(t['format'], mw, mh)
                    face.append(decode(t['format'], mw, mh, pr.take(face_start+consumed, n)))
                    consumed += n
                if consumed > size:
                    raise FormatError('texture mip data exceeds entry size')
                t['levels'].append(face)
        result.append(t)
    return result


def ctxb(data):
    data = inflate(data)
    r = Reader(data)
    r.magic(0, b'ctxb')
    if r.u32(4) != len(data) or r.u32(8) != 1:
        raise FormatError('invalid CTXB header')
    return textures(r.take(r.u32(16), len(data)-r.u32(16)),
                    r.take(r.u32(20), len(data)-r.u32(20)))
