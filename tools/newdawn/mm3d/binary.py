"""Checked binary access, magic-based LzS and GAR readers (no game data)."""
import struct


class FormatError(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.data = memoryview(data)

    def take(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise FormatError(f'out of bounds: {offset:#x}+{size:#x}/{len(self.data):#x}')
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.take(offset, struct.calcsize(fmt)))

    def u8(self, offset):
        return self.unpack('<B', offset)[0]

    def u16(self, offset):
        return self.unpack('<H', offset)[0]

    def i16(self, offset):
        return self.unpack('<h', offset)[0]

    def u32(self, offset):
        return self.unpack('<I', offset)[0]

    def string(self, offset, limit=256):
        return bytes(self.take(offset, min(limit, len(self.data) - offset))).split(b'\0')[0].decode('ascii')

    def magic(self, offset, magic):
        if self.take(offset, len(magic)) != magic:
            raise FormatError(f'expected {magic!r} at {offset:#x}')


def inflate(data):
    """Okumura LZSS: zeroed 4096-byte ring, cursor 4078, LSB-first flags."""
    if data[:4] != b'LzS\x01':
        return data
    r = Reader(data)
    size, packed = r.unpack('<II', 8)
    if size > 512 * 1024 * 1024 or packed != len(data) - 16:
        raise FormatError('invalid LzS sizes')
    ring = bytearray(4096)
    cursor, src = 4078, 16
    out = bytearray()
    while len(out) < size:
        flags = r.u8(src)
        src += 1
        for bit in range(8):
            if len(out) == size:
                break
            if flags & (1 << bit):
                token = [r.u8(src)]
                src += 1
                for value in token:
                    out.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 4095
            else:
                a, b = r.unpack('<BB', src)
                src += 2
                pos, count = a | ((b & 240) << 4), (b & 15) + 3
                if len(out) + count > size:
                    raise FormatError('LzS match exceeds declared size')
                for j in range(count):
                    value = ring[(pos + j) & 4095]
                    out.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 4095
    if src != len(data):
        raise FormatError('unused LzS payload')
    return bytes(out)


def literal_lzs(data):
    """Synthetic literal encoder, used only by --self-check."""
    payload = b''.join(bytes([(1 << len(data[i:i+8])) - 1]) + data[i:i+8]
                       for i in range(0, len(data), 8))
    return b'LzS\x01' + struct.pack('<III', 0, len(data), len(payload)) + payload


def gar(data):
    data = inflate(data)
    r = Reader(data)
    r.magic(0, b'GAR\x02')
    if r.u32(4) != len(data):
        raise FormatError('GAR size mismatch')
    count = r.u16(10)
    meta, offsets = r.unpack('<II', 16)
    entries = {}
    for i in range(count):
        size, _, name = r.unpack('<III', meta + i * 12)
        key = r.string(name)
        if key in entries:
            raise FormatError(f'duplicate GAR path: {key}')
        entries[key] = bytes(r.take(r.u32(offsets + i * 4), size))
    return entries
