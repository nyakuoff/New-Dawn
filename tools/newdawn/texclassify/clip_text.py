"""Dev-time: text embeddings for prompts.py with the same CLIP model as clip_embed.py. Writes <work>/clip_text.npy + clip_text_idx.json."""
import json, os, sys
import numpy as np
import torch, open_clip
sys.path.insert(0, os.path.dirname(__file__))
import prompts
work = sys.argv[1]
mn, pt = (sys.argv[2], sys.argv[3]) if len(sys.argv) > 3 else ('ViT-B-32', 'laion2b_s34b_b79k')
model, _, _ = open_clip.create_model_and_transforms(mn, pretrained=pt)
tok = open_clip.get_tokenizer(mn)
model.eval()
rows, idx = [], []
for ci, c in enumerate(prompts.CLASSES):
    for ph in prompts.PROMPTS[c]:
        txt = [t.format(ph) for t in prompts.TEMPLATES]
        with torch.no_grad():
            e = model.encode_text(tok(txt))
            e = e / e.norm(dim=-1, keepdim=True)
            e = e.mean(0)
            e = e / e.norm()
        rows.append(e.numpy()); idx.append(ci)
np.save(os.path.join(work, 'clip_text.npy'), np.stack(rows))
json.dump(idx, open(os.path.join(work, 'clip_text_idx.json'), 'w'))
print(len(rows), 'prompt embeddings')
