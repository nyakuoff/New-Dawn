"""Read-only access to the player's own mm.o2r: CRC64 path hashes, texture / TLUT decode, Vtx arrays, display-list parsing.
Nothing here writes game data anywhere inside the repository."""
import re, struct, zipfile, os
import numpy as np

DEFAULT_O2R = os.environ.get('ND_MM_O2R', os.path.expanduser('~/dev/2ship2harkinian/build-cmake/mm/mm.o2r'))
_SRC = os.path.join(os.path.dirname(__file__), '..', '..', '..', 'libultraship', 'src', 'ship', 'utils', 'StrHash64.cpp')


def _crc_table():
    txt = open(_SRC).read()
    t = [int(x, 16) for x in re.findall(r'CONST64\((0x[0-9a-f]+)\)', txt)]
    assert len(t) == 256
    return t


_T = _crc_table()


def crc64(s):
    c = 0xFFFFFFFFFFFFFFFF
    for b in s.encode():
        c = _T[((c >> 56) ^ b) & 0xFF] ^ ((c << 8) & 0xFFFFFFFFFFFFFFFF)
    return c


FMT = {1: 'RGBA32', 2: 'RGBA16', 3: 'CI4', 4: 'CI8', 5: 'I4', 6: 'I8', 7: 'IA4', 8: 'IA8', 9: 'IA16'}


class Archive:
    def __init__(self, path=DEFAULT_O2R):
        self.z = zipfile.ZipFile(path)
        self.names = [i.filename for i in self.z.infolist() if not i.is_dir()]
        self.hash2name = {crc64(n): n for n in self.names}
        self._vtx, self._dl, self._tex = {}, {}, {}

    def read(self, name):
        return self.z.read(name)

    def rtype(self, name):
        d = self.z.read(name)
        return d[4:8][::-1].decode('latin1') if len(d) >= 8 else '?'

    # ---- textures -----------------------------------------------------
    def tex_raw(self, name):
        """(fmt, w, h, raw bytes) of an OTEX resource."""
        if name not in self._tex:
            d = self.z.read(name)
            fmt, w, h, sz = struct.unpack_from('<4I', d, 64)
            self._tex[name] = (fmt, w, h, d[80:80 + sz])
        return self._tex[name]

    def tex_header(self, name):
        d = self.z.read(name)
        return struct.unpack_from('<4I', d, 64)

    def tlut(self, name):
        """RGBA8 palette (n,4) of a TLUT resource (RGBA16 entries)."""
        fmt, w, h, raw = self.tex_raw(name)
        v = np.frombuffer(raw, '>u2').astype(np.uint32)
        return np.stack([((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31,
                         (v & 1) * 255], 1).astype(np.uint8)

    def decode(self, name, pal=None):
        """RGBA8 (h,w,4). CI textures need `pal` (n,4); without it they decode as a grey ramp (flag via returned bool)."""
        fmt, w, h, raw = self.tex_raw(name)
        grey = False
        if fmt == 1:
            a = np.frombuffer(raw, np.uint8).reshape(h, w, 4)
        elif fmt == 2:
            v = np.frombuffer(raw, '>u2').reshape(h, w).astype(np.uint32)
            a = np.dstack([((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31,
                           (v & 1) * 255])
        elif fmt in (3, 5, 7):
            b = np.frombuffer(raw, np.uint8)
            n = np.empty(b.size * 2, np.uint8)
            n[0::2] = b >> 4
            n[1::2] = b & 15
            n = n[:w * h].reshape(h, w)
            if fmt == 3:
                if pal is not None and len(pal) >= 16:
                    a = pal[n.astype(np.intp) % len(pal)]
                else:
                    g = n * 17
                    a = np.dstack([g, g, g, np.full_like(g, 255)])
                    grey = True
            elif fmt == 7:
                g = (n >> 1) * 255 // 7
                al = (n & 1) * 255
                a = np.dstack([g, g, g, al])
            else:
                g = n * 17
                a = np.dstack([g, g, g, np.full_like(g, 255)])
        elif fmt in (4, 6):
            g = np.frombuffer(raw, np.uint8).reshape(h, w)
            if fmt == 4:
                if pal is not None:
                    a = pal[g.astype(np.intp) % len(pal)]
                else:
                    a = np.dstack([g, g, g, np.full_like(g, 255)])
                    grey = True
            else:
                a = np.dstack([g, g, g, np.full_like(g, 255)])
        elif fmt == 8:
            b = np.frombuffer(raw, np.uint8).reshape(h, w)
            g = (b >> 4) * 17
            a = np.dstack([g, g, g, (b & 15) * 17])
        elif fmt == 9:
            b = np.frombuffer(raw, np.uint8).reshape(h, w, 2)
            a = np.dstack([b[..., 0]] * 3 + [b[..., 1]])
        else:
            raise ValueError('fmt %d' % fmt)
        return np.ascontiguousarray(a.astype(np.uint8)), grey

    # ---- vertex arrays ------------------------------------------------
    def vtx(self, name):
        if name not in self._vtx:
            d = self.z.read(name)
            n = struct.unpack_from('<I', d, 68)[0]
            dt = np.dtype([('x', '<i2'), ('y', '<i2'), ('z', '<i2'), ('f', '<u2'), ('s', '<i2'), ('t', '<i2'),
                           ('c', 'u1', 4)])
            self._vtx[name] = np.frombuffer(d, dt, n, 72)
        return self._vtx[name]

    # ---- display lists --------------------------------------------------
    def dl(self, name):
        """List of (w0, w1, extra) with extra = (hash) for the 128-bit OTR ops, else None."""
        if name in self._dl:
            return self._dl[name]
        d = self.z.read(name)
        pos = 72  # 64 byte header, ucode byte, pad to 8
        cmds = []
        EXP = (0x20, 0x31, 0x32, 0x35, 0x33, 0x36, 0x42)
        n = len(d)
        while pos + 8 <= n:
            w0, w1 = struct.unpack_from('<II', d, pos)
            pos += 8
            op = w0 >> 24
            ex = None
            if op in EXP:
                h0, h1 = struct.unpack_from('<II', d, pos)
                pos += 8
                ex = (h0 << 32) | h1
            cmds.append((w0, w1, ex))
            if op == 0xDF:
                break
        self._dl[name] = cmds
        return cmds


def scene_of(name):
    p = name.split('/')
    if p[0] == 'scenes':
        return 'scenes/%s/%s' % (p[1], p[2])
    if p[0] in ('objects', 'misc') and len(p) > 1:
        return '%s/%s' % (p[0], p[1])
    return p[0]
