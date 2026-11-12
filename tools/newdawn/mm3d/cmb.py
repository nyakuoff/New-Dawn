"""CMB v10 geometry, section, bone and draw-state reader for MM3D rooms."""
import numpy as np
from binary import Reader, FormatError
from texture import textures

DTYPES = {0x1400: 'i1', 0x1401: 'u1', 0x1402: '<i2', 0x1403: '<u2',
          0x1404: '<i4', 0x1405: '<u4', 0x1406: '<f4'}
ATTRS = ('position', 'normal', 'tangent', 'color', 'uv0', 'uv1', 'uv2', 'bone_indices', 'bone_weights')
DIMS = (3, 3, 3, 4, 2, 2, 2)


def materials(r, base):
    r.magic(base, b'mats')
    result = []
    for i in range(r.u32(base+8)):
        b = base+12+i*0x16c
        r.take(b, 0x16c)
        bindings = []
        for j in range(3):
            p = b+16+j*24
            bindings.append(dict(texture=r.i16(p), min_filter=r.u16(p+4), mag_filter=r.u16(p+6),
                                 wrap_s=r.u16(p+8), wrap_t=r.u16(p+10)))
        coordinators = []
        for j in range(3):
            p = b+0x58+j*24
            coordinators.append(dict(source=r.u8(p), camera=r.u8(p+1), mapping=r.u8(p+2),
                                     matrix_mode=r.u8(p+3), transform=list(r.unpack('<5f', p+4))))
        alpha, blend = bool(r.u8(b+0x130)), r.u8(b+0x138)
        count = r.u32(b+0x120)
        if count > 6:
            raise FormatError('too many material combiner stages')
        combiner_base = base+12+r.u32(base+8)*0x16c
        result.append(dict(index=i, layer=r.u8(b+3), cull=('none', 'back', 'front', 'none')[r.u8(b+4)],
                           bindings=bindings, coordinators=coordinators,
                           alpha_mode='blend' if blend else ('cutout' if alpha else 'opaque'),
                           alpha_test=alpha, alpha_reference=r.u8(b+0x131)/255,
                           alpha_function=r.u16(b+0x132), depth_test=bool(r.u8(b+0x134)),
                           depth_write=bool(r.u8(b+0x135)), depth_function=r.u16(b+0x136),
                           blend_mode=blend, blend_rgb=list(r.unpack('<3H', b+0x13c)),
                           blend_alpha=list(r.unpack('<3H', b+0x144)),
                           blend_constant=list(r.unpack('<4f', b+0x14c)),
                           polygon_offset=list(r.unpack('<BBbb', b+4)),
                           fragment_lighting=bool(r.u8(b)), vertex_lighting=bool(r.u8(b+1)),
                           fog=bool(r.u8(b+2)), diffuse=list(r.take(b+0xa8, 4)),
                           ambient=list(r.take(b+0xa4, 4)), emission=list(r.take(b+0xa0, 4)),
                           constants=[list(r.take(b+0xb4+4*j, 4)) for j in range(6)],
                           # Preserve numeric PICA combiners without inventing GL approximations.
                           combiners=[list(r.unpack('<20H', combiner_base+r.u16(b+0x124+2*j)*40))
                                      for j in range(count)]))
    return result


def parse(data, decode_textures=True):
    r = Reader(data)
    r.magic(0, b'cmb ')
    if r.u32(8) != 10:
        raise FormatError('only CMB v10 is supported')
    size = r.u32(4)
    r.take(0, size)
    r = Reader(r.take(0, size))
    sk, ma, tx, model, va, ix, td = (r.u32(p) for p in (0x24, 0x2c, 0x30, 0x34, 0x3c, 0x40, 0x44))
    index_data = Reader(r.take(ix, r.u32(0x20)*2))
    r.magic(sk, b'skl ')
    bones = []
    for i in range(r.u32(sk+8)):
        p = sk+16+i*44
        bone_id, parent, *srt = r.unpack('<Hh9f', p)
        bones.append(dict(id=bone_id & 4095, parent=parent, scale=srt[:3], rotation=srt[3:6],
                          translation=srt[6:], name_hash=r.u32(p+40)))
    # Hierarchical bind pose: Rz * Ry * Rx, then column scale, translation.
    bone_by_id = {b['id']: b for b in bones}
    matrices = {}
    pending = set()

    def matrix(bone_id):
        if bone_id in matrices:
            return matrices[bone_id]
        if bone_id in pending or bone_id not in bone_by_id:
            raise FormatError('cyclic/missing bone')
        pending.add(bone_id)
        bone = bone_by_id[bone_id]
        x, y, z = bone['rotation']
        cx, cy, cz = np.cos([x, y, z])
        sx, sy, sz = np.sin([x, y, z])
        rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
        ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
        rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
        m = np.eye(4)
        m[:3, :3] = rz @ ry @ rx @ np.diag(bone['scale'])
        m[:3, 3] = bone['translation']
        if bone['parent'] >= 0:
            m = matrix(bone['parent']) @ m
        pending.remove(bone_id)
        matrices[bone_id] = m
        return m

    for bone in bones:
        matrix(bone['id'])
    mats = materials(r, ma)
    tex = textures(r.data[tx:], r.data[td:] if td and decode_textures else None)
    r.magic(va, b'vatr')
    arrays = []
    for i in range(9):
        length, offset = r.unpack('<II', va+12+i*8)
        arrays.append(r.take(va+offset, length))
    r.magic(model, b'sklm')
    msh, shp = model+r.u32(model+8), model+r.u32(model+12)
    r.magic(msh, b'mshs')
    r.magic(shp, b'shp ')
    sections = []
    for s in range(r.u32(shp+8)):
        b = shp+r.u16(shp+16+s*2)
        r.magic(b, b'sepd')
        attrs = []
        for j in range(9):
            start, scale, typ, mode, *constant = r.unpack('<IfHH4f', b+0x24+j*28)
            if mode not in (0, 1) or typ not in DTYPES:
                raise FormatError('invalid vertex attribute type/mode')
            attrs.append(dict(start=start, scale=scale, type=typ, mode=mode, constant=constant))
        dim = r.u16(b+0x24+9*28)
        primitives = []
        for j in range(r.u16(b+8)):
            p = b+r.u16(b+0x24+9*28+4+j*2)
            r.magic(p, b'prms')
            if r.u32(p+8) != 1:
                raise FormatError('multiple prm per prms unsupported')
            mode, n = r.unpack('<HH', p+12)
            table = list(r.unpack('<'+str(n)+'H', p+r.u32(p+16)))
            q = p+r.u32(p+20)
            r.magic(q, b'prm ')
            typ, count, start = r.u16(q+16), r.u16(q+20), r.u16(q+22)*2
            if typ not in (0x1401, 0x1403) or count % 3:
                raise FormatError('non-triangle or unsupported indices')
            dtype = np.dtype(DTYPES[typ])
            inds = np.frombuffer(index_data.take(start, count*dtype.itemsize), dtype=dtype).astype('<u4')
            primitives.append(dict(skinning=mode, bone_table=table, indices=inds))
        sections.append(dict(attributes=attrs, bone_dimension=dim, primitives=primitives,
                             vertex_colors=bool(r.u16(b+10) & 8),
                             center=list(r.unpack('<3f', b+12)), offset=list(r.unpack('<3f', b+24))))
    vertices, indices, draws = [], [], []
    nv, ni = 0, 0
    for i in range(r.u32(msh+8)):
        p = msh+16+i*12
        section, material = r.u16(p), r.u8(p+2)
        if section >= len(sections) or material >= len(mats):
            raise FormatError('mesh references missing section/material')
        sec = sections[section]
        for primitive in sec['primitives']:
            inds = primitive['indices']
            if len(inds) == 0:
                continue
            # Remap only referenced vertices; this both bounds-checks attributes and
            # avoids copying unused tails of the shared vatr streams.
            used, local = np.unique(inds, return_inverse=True)
            active_uvs = {co['source'] for bind, co in zip(mats[material]['bindings'], mats[material]['coordinators'])
                          if bind['texture'] >= 0 and co['mapping'] == 1}
            values = []
            for a, (attr, stream) in enumerate(zip(sec['attributes'], arrays)):
                dims = DIMS[a] if a < 7 else sec['bone_dimension']
                if dims < 1 or dims > 4:
                    raise FormatError('invalid bone dimension')
                if (attr['mode'] == 1 or not len(stream) or
                    (a == 7 and primitive['skinning'] == 0) or
                    (a == 8 and primitive['skinning'] != 2) or
                    (a in (4, 5, 6) and a-4 not in active_uvs)):
                    v = np.tile(attr['constant'][:dims], (len(used), 1)).astype(np.float32)
                else:
                    dt = np.dtype(DTYPES[attr['type']])
                    sr = Reader(stream)
                    count = int(used[-1])+1
                    try:
                        raw = np.frombuffer(sr.take(attr['start'], count*dims*dt.itemsize), dtype=dt)
                    except FormatError as exc:
                        raise FormatError(f'section {section} attribute {ATTRS[a]}: {exc}') from exc
                    v = raw.reshape(-1, dims)[used].astype(np.float32)
                    if attr['type'] != 0x1406:
                        v *= attr['scale']
                if a in (4, 5, 6) and a-4 not in active_uvs:
                    v = np.zeros_like(v)  # unused UV streams can contain NaNs/short tails
                values.append(v)
            pos, normal = values[0], values[1]
            table = primitive['bone_table']
            if primitive['skinning'] not in (0, 1, 2):
                raise FormatError('unknown skinning mode')
            if primitive['skinning'] != 2 and table:
                # Smooth skinning uses bind * inverse-bind at rest (identity).
                # Rigid/single-bone vertices need their hierarchical bind transform.
                slots = values[7][:, 0].astype(int) if primitive['skinning'] == 1 else np.zeros(len(used), dtype=int)
                if (slots < 0).any() or (slots >= len(table)).any():
                    raise FormatError('bone slot outside primitive palette')
                pos, normal = pos.copy(), normal.copy()
                for slot in np.unique(slots):
                    m = matrix(table[slot])
                    mask = slots == slot
                    pos[mask] = pos[mask] @ m[:3, :3].T + m[:3, 3]
                    normal[mask] = normal[mask] @ np.linalg.inv(m[:3, :3])
            lengths = np.linalg.norm(normal, axis=1, keepdims=True)
            normal = np.divide(normal, lengths, out=np.zeros_like(normal), where=lengths > 1e-10)
            color = values[3] if sec['vertex_colors'] else np.ones((len(used), 4), dtype=np.float32)
            vbo = np.concatenate((pos, normal, values[4], color), axis=1).astype('<f4')
            if not np.isfinite(vbo).all() or np.abs(pos).max() > 1e6:
                raise FormatError(f'nonfinite attributes or unreasonable position in section {section}: range {pos.min(0)}..{pos.max(0)}, finite={np.isfinite(vbo).all(0)}')
            vertices.append(vbo)
            indices.append(local.astype('<u4')+nv)
            draws.append(dict(first_index=ni, index_count=len(inds), material=material,
                              section=section, skinning=primitive['skinning'], bone_table=table,
                              vertex_first=nv, vertex_count=len(used),
                              extra_uvs={str(k): values[k+4].tolist() for k in sorted(active_uvs) if k in (1, 2)}))
            nv += len(used)
            ni += len(inds)
    vbo = np.concatenate(vertices) if vertices else np.empty((0, 12), dtype='<f4')
    ibo = np.concatenate(indices) if indices else np.empty(0, dtype='<u4')
    if len(ibo) and ibo.max() >= len(vbo):
        raise FormatError('out of range exported index')
    for mat in mats:
        for binding in mat['bindings']:
            if binding['texture'] >= len(tex):
                raise FormatError('out of range material texture')
    return dict(name=r.string(16, 16), vertices=vbo, indices=ibo, draws=draws, materials=mats,
                textures=tex, bones=bones, sections=[{k: v for k, v in s.items() if k != 'primitives'} for s in sections])
