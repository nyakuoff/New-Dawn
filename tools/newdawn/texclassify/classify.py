"""Automatic classification: CLIP zero-shot prior + usage evidence (wrap/clamp, alpha mode, envmap) + image statistics + name hints.
Reads <work>/feat.json, clip_*.npy; writes <work>/auto.json {path: {class, conf, probs(top3), why}}.  Review overrides are applied later."""
import json, os, re, sys, collections
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from prompts import CLASSES

CI = {c: i for i, c in enumerate(CLASSES)}
MATERIAL = [c for c in CLASSES if c not in ('unique', 'skip')]

# object names that tell what a texture is (character parts, symbols, effects)
NAME_UNIQUE = re.compile(r'(Eye|Mouth|Face|Hair|Head|Brow|Pupil|Iris|Tooth|Teeth|Tongue|Nose|Ear|Emblem|Symbol|Sign|Crest|Logo|'
                         r'Mask|Portrait|Painting|Picture|Letter|Text|Rune|Glyph|Icon|Map|Poster|Banner|Flag|Tatl|Skull|Mouth|Cheek)', re.I)
NAME_SKIP = re.compile(r'(Flame|Fire|Smoke|Spark|Glow|Flare|Light(?!house)|Beam(?!s)|Shadow|Ripple|Splash|Bubble|Dust|Cloud|Mist|Fog|'
                       r'Star|Shine|Glint|Twinkle|Ray|Aura|Blast|Explo|Lightning|Thunder|Rain|Drop|Wave(?!s)|Foam|Steam|Gradient|Fade)', re.I)
NAME_MAT = [('wood_planks', r'(Plank|Board|Crate|Wooden|Floor|Door)'), ('rock', r'(Rock|Boulder|Stone)'),
            ('metal', r'(Metal|Iron|Steel|Chain|Grate|Bar)'), ('water', r'Water'), ('ice_snow', r'(Ice|Snow|Frost)'),
            ('fabric', r'(Cloth|Fabric|Carpet|Curtain|Cape|Sack)'), ('rope', r'Rope')]


def load(work):
    feat = json.load(open(os.path.join(work, 'feat.json')))
    names = json.load(open(os.path.join(work, 'clip_names.json')))
    es = np.load(os.path.join(work, 'clip_single.npy')); et = np.load(os.path.join(work, 'clip_tile.npy'))
    T = np.load(os.path.join(work, 'clip_text.npy')); ti = np.array(json.load(open(os.path.join(work, 'clip_text_idx.json'))))
    return feat, names, es, et, T, ti


def clip_probs(es, et, T, ti, scale=100.0):
    """class probabilities: for every class the best-matching prompt, averaged over the single and the tiled view."""
    out = []
    for e in (es, et):
        lg = e @ T.T * scale
        c = np.stack([lg[:, ti == k].max(1) for k in range(len(CLASSES))], 1)
        c = np.exp(c - c.max(1, keepdims=True))
        out.append(c / c.sum(1, keepdims=True))
    return out   # [single, tiled]


def classify(work, wmin=None):
    feat, names, es, et, T, ti = load(work)
    Ps, Pt = clip_probs(es, et, T, ti)
    res = {}
    for i, n in enumerate(names):
        f = feat[n]
        base = n.split('/')[-1]
        obj = n.startswith('objects/')
        # material-looking => tile view is informative; unique/skip are judged on the single view
        mat_p = Pt[i].copy()
        mat_p[CI['unique']] = Ps[i][CI['unique']]
        mat_p[CI['skip']] = Ps[i][CI['skip']]
        logp = np.log(0.5 * (Ps[i] + Pt[i]) * 0.5 + 0.5 * mat_p * 0.5 + 1e-6)
        logp[CI['skip']] -= 2.2   # CLIP's skip/unique prompts match almost any tiny texture: only evidence below may lift them
        logp[CI['unique']] -= 0.9
        why = []
        clamp, tg = f.get('clamp', 0), f.get('texgen', 0)
        used = f.get('use_a0', 0) + f.get('use_a1', 0) > 0
        tiling = used and f.get('wrapfrac', 0) > 0.5 and f.get('clamp', 0) < 0.3
        # --- usage evidence -------------------------------------------------
        if used and clamp >= 0.5 and f.get('clamp2', 0) >= 0.5:
            for c in MATERIAL:
                logp[CI[c]] -= 1.2
            logp[CI['unique']] += 1.0; logp[CI['skip']] += 0.5
            why.append('clamp')
        if f.get('am_xlu', 0) >= 0.6:
            for c in MATERIAL:
                logp[CI[c]] -= 1.0
            logp[CI['skip']] += 1.0
            logp[CI['water']] += 0.8
            why.append('xlu')
        if tg >= 0.5:
            for c in MATERIAL:
                logp[CI[c]] -= 0.8
            logp[CI['skip']] += 1.0
            logp[CI['metal']] += 0.6
            why.append('envmap')
        if f.get('am_cutout', 0) >= 0.5 or f['alpha_lo'] > 0.15:
            for c in MATERIAL:
                logp[CI[c]] -= 1.0
            logp[CI['foliage']] += 2.0 if (f['hue_green'] > 0.25 or f['hue_yellow'] + f['hue_brown'] > 0.3) else 1.2
            logp[CI['unique']] += 0.5
            why.append('cutout')
        if tiling:
            logp[CI['unique']] -= 0.5
            for c in MATERIAL:
                logp[CI[c]] += 0.3
            why.append('tiles')
        # --- image statistics -----------------------------------------------
        if f['range'] < 0.04 and f['edge'] < 0.01:
            logp[CI['skip']] += 1.5; why.append('flat')
        if f['ncol'] <= 3 and f['range'] < 0.1:
            logp[CI['skip']] += 1.0
        if f['lr_sym'] > 0.85 and f['border_std'] < 0.5 * f['centre_std'] and f['centre_std'] > 0.1:
            logp[CI['unique']] += 0.8; why.append('emblem-shape')
        # --- names (objects carry real names) -----------------------------------
        if obj and NAME_UNIQUE.search(base):
            logp[CI['unique']] += 2.0; why.append('name-unique')
        if obj and NAME_SKIP.search(base):
            logp[CI['skip']] += 2.0; why.append('name-skip')
        for c, rx in NAME_MAT:
            if obj and re.search(rx, base):
                logp[CI[c]] += 0.7; why.append('name-' + c)
        if f.get('nopal'):
            why.append('nopal')
        p = np.exp(logp - logp.max()); p /= p.sum()
        o = np.argsort(-p)
        res[n] = dict(c=CLASSES[o[0]], conf=round(float(p[o[0]]), 3), top=[[CLASSES[j], round(float(p[j]), 3)] for j in o[:3]],
                      why=why)
    return res


if __name__ == '__main__':
    work = sys.argv[1]
    r = classify(work)
    json.dump(r, open(os.path.join(work, 'auto.json'), 'w'))
    c = collections.Counter(v['c'] for v in r.values())
    print(sorted(c.items(), key=lambda x: -x[1]))
