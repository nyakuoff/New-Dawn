"""MM ZSI scene/room setup commands and native collision meshes."""
import numpy as np
from binary import Reader, FormatError, inflate


def collision(r, p):
    nv, npoly, ns, nc, nw = r.unpack('<5H', p+14)
    vp, pp, sp, cp, wp = r.unpack('<5I', p+24)
    vertices = np.frombuffer(r.take(vp, nv*6), dtype='<i2').reshape(-1, 3).astype(np.float64)
    records = np.frombuffer(r.take(pp, npoly*20), dtype=np.dtype([
        ('type', '<u2'), ('indices', '<u2', (3,)), ('normal', '<i2', (3,)),
        ('pad', '<u2'), ('distance', '<f4')]))
    indices = records['indices'].astype(np.int32) & 8191
    if len(indices) and indices.max() >= nv:
        raise FormatError('ZSI collision index out of bounds')
    if not np.isfinite(records['distance']).all():
        raise FormatError('nonfinite collision plane')
    return dict(vertices=vertices, indices=indices, normals=records['normal'].astype(float)/32767,
                bounds=list(r.unpack('<6h', p+2)), surfaces=bytes(r.take(sp, ns*8)).hex(),
                camera_count=nc, camera_pointer=cp, water_count=nw, water_pointer=wp)


def parse(data, collision_only=False):
    data = inflate(data)
    r0 = Reader(data)
    r0.magic(0, b'ZSI\x09')
    r = Reader(r0.data[16:])
    visited = set()

    def setup(start):
        if start in visited:
            raise FormatError('cyclic/shared ZSI setup header')
        visited.add(start)
        result = dict(header=start, rooms=[], meshes=[], environment=[], commands=[], setups=[], setup_refs=[])
        for p in range(start, len(r.data), 8):
            op, count, _, arg = r.unpack('<BBHI', p)
            result['commands'].append(dict(opcode=op, count=count, arg=arg))
            if op == 0x14:
                return result
            if op == 0x04:
                result['rooms'] = [r.string(arg+i*0x34, 0x34) for i in range(count)]
            elif op == 0x03:
                result['collision'] = collision(r, arg)
                if collision_only:
                    return result
            elif op == 0x0a:
                typ, n = r.u8(arg), r.u8(arg+1)
                if n and typ != 2:
                    raise FormatError(f'unsupported mesh type {typ}')
                entries = r.u32(arg+4)
                for i in range(n):
                    p0 = entries+i*16
                    for part, offset in zip(('opaque', 'transparent'), (8, 12)):
                        ptr = r.u32(p0+offset)
                        if ptr:
                            r.magic(ptr, b'cmb ')
                            result['meshes'].append(dict(part=part, entry=i, data=bytes(r.take(ptr, r.u32(ptr+4)))))
            elif op == 0x0f:
                for i in range(count):
                    b = arg+i*32
                    fog = r.u16(b+22)
                    result['environment'].append(dict(actor_ambient=list(r.take(b, 3)),
                        scene_ambient=list(r.take(b+3, 3)), light0_direction=list(r.unpack('<3b', b+6)),
                        light0_color=list(r.take(b+9, 3)), light1_direction=list(r.unpack('<3b', b+12)),
                        light1_color=list(r.take(b+15, 3)), fog_color=list(r.take(b+18, 3)),
                        fog_start=fog & 1023, blend_rate=(fog >> 10)*4,
                        fog_end=r.unpack('<f', b+24)[0], draw_distance=r.unpack('<f', b+28)[0]))
            elif op == 0x18:
                for i in range(count):
                    pointer = r.u32(arg+4*i)
                    result['setup_refs'].append(dict(slot=i, header=pointer if pointer else None))
                    if pointer and pointer not in visited:
                        result['setups'].append(setup(pointer))
        raise FormatError('unterminated ZSI commands')

    return dict(name=r0.string(4, 12), **setup(0))


def setups(scene):
    yield scene
    for child in scene['setups']:
        yield from setups(child)
