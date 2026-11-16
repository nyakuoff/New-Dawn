"""Condense the ID-buffer capture TSVs (texture spike, one per scene and yaw) into per-scene on-screen pixel shares.
Output is names and numbers only: {scene: {texture path: share of that scene's world+object textured pixels}}."""
import glob, json, os, re, sys, collections

SCENES = {  # capture name -> scene folder owning the room meshes
    'south_clock_town': 'scenes/nonmq/Z2_CLOCKTOWER', 'east_clock_town': 'scenes/nonmq/Z2_TOWN',
    'termina_field': 'scenes/nonmq/Z2_00KEIKOKU', 'southern_swamp': 'scenes/nonmq/Z2_20SICHITAI',
    'woodfall_temple': 'scenes/nonmq/Z2_MITURIN', 'snowhead_temple': 'scenes/nonmq/Z2_HAKUGIN',
    'great_bay_coast': 'scenes/nonmq/Z2_30GYOSON', 'stock_pot_inn': 'scenes/nonmq/Z2_YADOYA',
    'ikana_canyon': 'scenes/nonmq/Z2_IKANA', 'stone_tower_temple': 'scenes/nonmq/Z2_INISIE_N'}


def main(cov_dir, out):
    res = {}
    for name, folder in SCENES.items():
        files = sorted(glob.glob('%s/%s_*.tsv' % (cov_dir, name)))
        acc = collections.defaultdict(float)
        for f in files:
            L = open(f).read().splitlines()
            for l in L[1:]:
                c = l.split('\t')
                p = c[5]
                if p.startswith(('scenes/', 'objects/', 'misc/scene_texture')):
                    acc[p] += int(c[1])
        tot = sum(acc.values())
        res[folder] = dict(name=name, captures=len(files),
                           share={p: round(v / tot, 6) for p, v in sorted(acc.items(), key=lambda x: -x[1]) if v / tot >= 1e-4})
    json.dump(res, open(out, 'w'), separators=(',', ':'))
    print({k: (v['captures'], len(v['share'])) for k, v in res.items()})


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
