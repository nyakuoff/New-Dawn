"""Dev-time only: zero-shot CLIP image embeddings for every decoded texture (single image and a 2x2 tiled view).
Needs torch + open_clip (not part of the game, not shipped).  Writes <work>/clip_names.json, clip_single.npy, clip_tile.npy."""
import json, os, pickle, sys, time
import numpy as np
import torch, open_clip
from PIL import Image

work = sys.argv[1]
model_name, pretrained = (sys.argv[2], sys.argv[3]) if len(sys.argv) > 3 else ('ViT-B-32', 'laion2b_s34b_b79k')
torch.set_num_threads(8)
model, _, pre = open_clip.create_model_and_transforms(model_name, pretrained=pretrained)
model.eval()
imgs = pickle.load(open(os.path.join(work, 'tex.pkl'), 'rb'))
names = sorted(imgs)
mean = np.array(open_clip.OPENAI_DATASET_MEAN, np.float32); std = np.array(open_clip.OPENAI_DATASET_STD, np.float32)


def prep(a, tile):
    rgb = a[..., :3].astype(np.float32); al = a[..., 3:4].astype(np.float32) / 255
    rgb = rgb * al + 128 * (1 - al)             # neutral grey behind transparency
    im = Image.fromarray(rgb.astype(np.uint8))
    if tile:
        w, h = im.size
        t = Image.new('RGB', (w * 2, h * 2))
        for dx in (0, w):
            for dy in (0, h):
                t.paste(im, (dx, dy))
        im = t
    im = im.resize((224, 224), Image.BICUBIC)
    x = (np.asarray(im, np.float32) / 255 - mean) / std
    return torch.from_numpy(x.transpose(2, 0, 1))


out = {}
t0 = time.time()
for tile in (False, True):
    embs = []
    for i in range(0, len(names), 64):
        b = torch.stack([prep(imgs[n], tile) for n in names[i:i + 64]])
        with torch.no_grad():
            e = model.encode_image(b)
            e = e / e.norm(dim=-1, keepdim=True)
        embs.append(e.numpy())
        if (i // 64) % 20 == 0:
            print('tile' if tile else 'single', i, len(names), round(time.time() - t0), 's', file=sys.stderr)
    np.save(os.path.join(work, 'clip_%s.npy' % ('tile' if tile else 'single')), np.concatenate(embs))
json.dump(names, open(os.path.join(work, 'clip_names.json'), 'w'))
print('done', round(time.time() - t0), 's', file=sys.stderr)
