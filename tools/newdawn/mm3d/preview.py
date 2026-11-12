"""Small deterministic, unlit orthographic software preview; stdlib PNG output."""
import struct
import zlib
import numpy as np


def png(path, rgba):
    h, w, _ = rgba.shape

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag+data))

    raw = b''.join(b'\0'+row.tobytes() for row in rgba.astype(np.uint8))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w, h, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def render(meshes, path, size=1000, top=False):
    # Oblique parallel view; no culling, static first setup, no gameplay actors.
    direction = np.array([0., 1., 0.]) if top else np.array([.4, .85, .35])
    direction /= np.linalg.norm(direction)
    right = np.array([1., 0., 0.]) if top else np.cross([0., 1., 0.], direction)
    right /= np.linalg.norm(right)
    up = np.cross(direction, right)
    basis = np.stack((right, up, direction), axis=1)
    coords = [m['vertices'][:, :3] @ basis for m in meshes if len(m['vertices'])]
    if not coords:
        raise ValueError('cannot preview empty geometry')
    allp = np.concatenate(coords)
    lo, hi = allp[:, :2].min(0), allp[:, :2].max(0)
    scale = (size-40)/max(hi-lo)
    center = (lo+hi)*.5
    image = np.full((size, size, 4), [23, 29, 39, 255], dtype=np.uint8)
    depth = np.full((size, size), -np.inf)
    for m in meshes:
        v = m['vertices']
        p = v[:, :3] @ basis
        p[:, :2] = (p[:, :2]-center)*scale+size*.5
        p[:, 1] = size-p[:, 1]
        for draw in m['draws']:
            mat = m['materials'][draw['material']]
            ti = mat['bindings'][0]['texture']
            tex = m['textures'][ti]['levels'][0][0] if ti >= 0 and m['textures'][ti]['levels'] else None
            coord = mat['coordinators'][0]
            ss, st, ts, tt, rot = coord['transform']
            c, s = np.cos(rot*np.pi), np.sin(rot*np.pi)
            uv = v[:, 6:8].copy()
            uv = (uv-.5) @ np.array([[ss*c, st*s], [-ss*s, st*c]]) + np.array([ss*(.5-ts), st*(.5-tt)])
            inds = m['indices'][draw['first_index']:draw['first_index']+draw['index_count']].reshape(-1, 3)
            for ids in inds:
                t = p[ids]
                xmin, ymin = np.maximum(np.floor(t[:, :2].min(0)).astype(int), 0)
                xmax, ymax = np.minimum(np.ceil(t[:, :2].max(0)).astype(int), size-1)
                if xmin > xmax or ymin > ymax:
                    continue
                a, b, c0 = t
                den = (b[1]-c0[1])*(a[0]-c0[0])+(c0[0]-b[0])*(a[1]-c0[1])
                if abs(den) < 1e-8:
                    continue
                yy, xx = np.mgrid[ymin:ymax+1, xmin:xmax+1]
                xx, yy = xx+.5, yy+.5
                w0 = ((b[1]-c0[1])*(xx-c0[0])+(c0[0]-b[0])*(yy-c0[1]))/den
                w1 = ((c0[1]-a[1])*(xx-c0[0])+(a[0]-c0[0])*(yy-c0[1]))/den
                w2 = 1-w0-w1
                z = w0*a[2]+w1*b[2]+w2*c0[2]
                d = depth[ymin:ymax+1, xmin:xmax+1]
                mask = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6) & (z > d)
                if not mask.any():
                    continue
                weights = np.stack((w0[mask], w1[mask], w2[mask]), axis=1)
                color = np.clip(weights @ v[ids, 8:12], 0, 1)
                if tex is not None:
                    uvs = weights @ uv[ids]
                    for k, wrap in enumerate((mat['bindings'][0]['wrap_s'], mat['bindings'][0]['wrap_t'])):
                        if wrap == 0x2901:
                            uvs[:, k] %= 1
                        elif wrap == 0x8370:
                            uvs[:, k] = 1-np.abs(uvs[:, k] % 2-1)
                        else:
                            uvs[:, k] = np.clip(uvs[:, k], 0, 1)
                    tx = np.minimum((uvs[:, 0]*tex.shape[1]).astype(int), tex.shape[1]-1)
                    ty = np.minimum((uvs[:, 1]*tex.shape[0]).astype(int), tex.shape[0]-1)
                    color *= tex[ty, tx]/255
                else:
                    color[:, :3] *= [.65, .78, .88]
                # Diagnostic approximation: texture0 * vertex colour. Blended geometry
                # uses a simple alpha blend, not an exact PICA material evaluation.
                keep = color[:, 3] >= (mat['alpha_reference'] if mat['alpha_test'] else .01)
                ys, xs = np.where(mask)
                ys, xs = ys[keep]+ymin, xs[keep]+xmin
                color = color[keep]
                if mat['alpha_mode'] == 'blend':
                    rgb = color[:, :3]*color[:, 3:4]+image[ys, xs, :3]/255*(1-color[:, 3:4])
                else:
                    rgb = color[:, :3]
                image[ys, xs, :3] = np.clip(rgb*255, 0, 255).astype(np.uint8)
                if mat['depth_write']:
                    depth[ys, xs] = z[ys-ymin, xs-xmin]
    png(path, image)
