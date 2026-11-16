"""Schema self-check of the committed tables (no game data needed).  usage: python3 tools/newdawn/texclassify/check.py [dir]"""
import json, os, sys
D = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', '..', 'mm', 'assets', 'custom', 'newdawn_textures')
L = lambda n: json.load(open(os.path.join(D, n)))
C = L('classification.json'); classes = set(C['classes'])
assert len(C['classes']) == 22 and C['version'] == 1
for p, e in C['textures'].items():
    assert p.split('/')[0] in ('scenes', 'objects', 'misc') and 'TLUT' not in p, p
    assert e['class'] in classes and 0 <= e['conf'] <= 1 and e['rv'] in (0, 1), p
    assert 'pal' not in e or 'TLUT' in e['pal'], p
S = L('scale.json')
for p, e in S['textures'].items():
    assert p in C['textures'] and min(e['tpu']) > 0 and e['space'] in ('scene', 'object'), p
Y = L('layers.json'); roles = set(Y['roles'])
for e in Y['pairs']:
    assert e['a'] in C['textures'] and e['b'] in C['textures'] and e['a'] != e['b'] and set(e['roles']) <= roles and e['pattern'] in Y['patterns'], e
M = L('mm3d_match.json')
for p, e in M['textures'].items():
    assert C['textures'][p]['class'] == 'unique' or C['textures'][p]['conf'] < 0.6, p
    assert 0 <= e['score'] <= 1 and -1e-9 <= e['margin'] <= 1, p
R = L('scene_stats.json')
for o, e in R['scenes'].items():
    a = e['area']; assert abs(sum(a.values()) - 1) < 0.01, o
    if 'screen' in e: assert abs(sum(e['screen'][k] for k in ('replaceable', 'unique', 'skip')) - 1) < 0.01, o
print('ok:', len(C['textures']), 'textures,', len(S['textures']), 'scales,', len(Y['pairs']), 'layer pairs,', len(M['textures']), '3DS matches,', len(R['scenes']), 'scenes')
