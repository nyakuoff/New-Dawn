# Offline authoring (private): Mantaflow noise grids -> emission/absorption ray-march along Y -> RGBA flipbook sheet.
# R = flame heat (core brightness), A = coverage. usage: render_sheet.py <bakedir> <out.png> <first> <count> <cols> <cw> <ch>
import gzip, struct, sys, numpy as np
from PIL import Image, ImageFilter
bake, out, first, count, cols, cw, ch = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7])
def load(f):
    raw = gzip.open(f).read()
    gx, gy, gz = struct.unpack('<3i', raw[4:16])
    return np.frombuffer(raw[len(raw) - gx * gy * gz * 4:], dtype=np.float32).reshape(gz, gy, gx)
def march(g, k_abs=2.0):
    # view along +Y: arrays are (z, y, x); front-to-back emission/absorption
    gz, gy, gx = g.shape
    trans = np.ones((gz, gx), np.float32); em = np.zeros((gz, gx), np.float32); core = np.zeros((gz, gx), np.float32)
    for y in range(gy):
        s = g[:, y, :]
        a = 1 - np.exp(-k_abs * s / gy * 8)
        em += trans * a * s
        core += trans * a * s * s
        trans *= 1 - a
    alpha = 1 - trans
    return em, core, alpha
LOOPFADE = 8
frames = []
for fr in range(first, first + count + LOOPFADE):
    g = load(f'{bake}/cache/noise/flame_noise_{fr:04d}.uni')
    frames.append(march(g))
# fixed crop from the union of the covered area so every frame keeps the same framing
# Seamless loop: the last LOOPFADE source frames fade into the first LOOPFADE, so frame count-1 -> frame 0 is continuous.
def blend(a, b, t):
    return tuple(x * (1 - t) + y * t for x, y in zip(a, b))
tail = frames[count:]
frames = frames[:count]
for i in range(LOOPFADE):
    frames[i] = blend(tail[i], frames[i], i / LOOPFADE)
A = np.max([f[2] for f in frames], axis=0)
ys, xs = np.where(A > 0.04)
y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
pad = 3
y0, x0 = max(0, y0 - pad), max(0, x0 - pad); y1, x1 = min(A.shape[0], y1 + pad), min(A.shape[1], x1 + pad)
# match target aspect by widening around the centre
want = cw / ch; h = y1 - y0; w = x1 - x0
if w / h < want:
    cx = (x0 + x1) / 2; w2 = h * want; x0, x1 = int(cx - w2 / 2), int(cx + w2 / 2)
x0 = max(0, x0); x1 = min(A.shape[1], x1)
print('crop', (x0, x1, y0, y1), 'of', A.shape)
heat = np.max([f[0][y0:y1, x0:x1] for f in frames]); coreN = np.max([f[1][y0:y1, x0:x1] for f in frames])
sheet = Image.new('RGBA', (cols * cw, ((count + cols - 1) // cols) * ch), (0, 0, 0, 0))
for i, (em, core, alpha) in enumerate(frames):
    r = np.clip((em[y0:y1, x0:x1] / heat) ** 0.9 * 0.55 + (core[y0:y1, x0:x1] / coreN) ** 1.3 * 0.55, 0, 1)
    r = np.flipud(r); a = np.flipud(np.clip((alpha[y0:y1, x0:x1] * 1.3) ** 1.1, 0, 1))
    rgba = np.dstack([r, r, r, a])
    im = Image.fromarray((rgba * 255).astype(np.uint8), 'RGBA').resize((cw, ch), Image.LANCZOS)
    sheet.paste(im, ((i % cols) * cw, (i // cols) * ch))
sheet.save(out)
print('sheet', out, sheet.size)
