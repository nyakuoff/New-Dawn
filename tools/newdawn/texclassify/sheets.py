"""Contact sheets for visual review.  Each cell: the decoded texture (palette applied, alpha on a checker) with its index;
a manifest JSON next to the PNG maps index -> resource path so corrections can be written as `index:class`.
Output stays outside the repository (game-derived images)."""
import json, os, pickle, sys
import numpy as np
from PIL import Image, ImageDraw


def checker(h, w):
    y, x = np.mgrid[:h, :w]
    c = (((x // 4) + (y // 4)) % 2) * 40 + 90
    return np.dstack([c, c, c]).astype(np.float32)


def cell(a, size, tile=False):
    rgb = a[..., :3].astype(np.float32); al = a[..., 3:4].astype(np.float32) / 255
    h, w = a.shape[:2]
    if tile:
        rgb = np.tile(rgb, (2, 2, 1)); al = np.tile(al, (2, 2, 1)); h, w = h * 2, w * 2
    bg = checker(h, w)
    im = Image.fromarray((rgb * al + bg * (1 - al)).astype(np.uint8))
    s = size / max(h, w)
    nw, nh = max(1, int(round(w * s))), max(1, int(round(h * s)))
    im = im.resize((nw, nh), Image.NEAREST if s >= 2 else Image.BICUBIC)
    out = Image.new('RGB', (size, size), (24, 24, 24))
    out.paste(im, ((size - nw) // 2, (size - nh) // 2))
    return out


def sheet(items, imgs, out_png, cols=14, size=92, label=None, start=0, tile=False):
    """items: list of resource paths.  label(i, path) -> text under the cell.  Returns manifest {index: path}."""
    rows = (len(items) + cols - 1) // cols
    lh = 11
    W, H = cols * (size + 4) + 4, rows * (size + lh + 4) + 4
    im = Image.new('RGB', (W, H), (10, 10, 10))
    d = ImageDraw.Draw(im)
    man = {}
    for k, p in enumerate(items):
        r, c = divmod(k, cols)
        x, y = 4 + c * (size + 4), 4 + r * (size + lh + 4)
        im.paste(cell(imgs[p], size, tile), (x, y))
        t = label(start + k, p) if label else str(start + k)
        d.text((x + 1, y + size), t, fill=(235, 235, 120))
        man[start + k] = p
    im.save(out_png)
    json.dump(man, open(out_png[:-4] + '.json', 'w'))
    return man


if __name__ == '__main__':
    # sheets.py <work> <auto.json> <class> <outdir> [per_sheet] [sortkey]
    work, auto, cls, outdir = sys.argv[1:5]
    per = int(sys.argv[5]) if len(sys.argv) > 5 else 140
    imgs = pickle.load(open(os.path.join(work, 'tex.pkl'), 'rb'))
    feat = json.load(open(os.path.join(work, 'feat.json')))
    A = json.load(open(auto))
    items = [n for n, v in A.items() if v['c'] == cls]
    items.sort(key=lambda n: -(feat[n].get('use_a0', 0) + feat[n].get('use_a1', 0)))
    os.makedirs(outdir, exist_ok=True)
    for s in range(0, len(items), per):
        sheet(items[s:s + per], imgs, '%s/%s_%03d.png' % (outdir, cls, s // per), start=s, label=lambda i, p: '%d %.2f' % (i, A[p]['conf']))
    print(cls, len(items))
