#!/usr/bin/env python3
"""Classify indexed materials into ND texture classes + Termina regions and give an automatic score.

usage: classify.py WORKDIR     (reads WORKDIR/index/*.json, writes WORKDIR/classified.json)
The automatic score (0-10) is only a pre-filter; the final score comes from looking at contact sheets
and is stored in triage.json (id -> {"score": n, "drop": reason}), applied by build_catalog.py.
"""
import json, re, sys
from pathlib import Path

# first match wins; text = "name cat tags"
CLASSES = [
    ("skip", r"marble|onyx|climbing|veneer|anti.?slip|laminate|sci-?fi|asphalt|road|plastic|food|pizza|christmas|ornament|foil|styrofoam|rubber|office|paper|skin|car paint|"
             r"glass|leaves|cutout|imperfection|decal|pattern|terrazzo|porcelain|diamond plate|bathroom|kitchen|ceramic|candy|lego"),
    ("roof_tiles", r"roof"),
    ("thatch", r"thatch|straw|hay\b|reed|wicker|bamboo|cork"),
    ("rope", r"\brope|\bnet\b|cord\b|burlap|hessian"),
    ("ice_snow", r"snow|\bice\b|frost"),
    ("moss", r"moss|lichen|ivy"),
    ("grass", r"grass|lawn|meadow"),
    ("mud", r"\bmud|swamp|bog|wet soil|wet ground"),
    ("sand", r"\bsand\b|beach|dune"),
    ("gravel", r"gravel|pebble"),
    ("brick", r"brick"),
    ("cobble", r"cobble|sett\b|paving stones|pavers?\b|stone pav|pavement"),
    ("flagstone", r"flagstone|slabs?\b|travertine|limestone|sandstone|natural stone|stone tiles?|stone floor|\btiles?\b"),
    ("rock", r"\brocks?\b|cliff|boulder|slate|cracked earth|terrain|\bstones?\b.*(natural|rough|rugged)"),
    ("wood_planks", r"planks?|wood floor|parquet|floorboard|wood siding|fence|wood panel|painted wood|wooden floor|laminate"),
    ("bark", r"\bbark|\blog\b|tree trunk"),
    ("wood_beam", r"\bwood|timber|beam|\bold wood|raw wood"),
    ("plaster", r"plaster|stucco|facade|\bclay\b|concrete|cement|render\b|\bwall\b|paint"),
    ("worked_stone", r"marble|onyx|granite|\bstone|ashlar|masonry|block"),
    ("dirt", r"ground|dirt|soil|earth|clay|mud|field"),
    ("metal", r"metal|iron|steel|rust|corrugated|grate|grille|mesh|bronze|copper|brass|plates?\b|walkway"),
    ("carpet_rug", r"carpet|rug\b|kilm|kilim|patchwork"),
    ("wallpaper", r"wallpaper|damask|ikat|floral"),
    ("leather", r"leather"),
    ("fabric", r"fabric|cloth|linen|wool|velvet|curtain|canvas|felt|denim|fleece|corduroy|cotton|weave|knit|textile|ramie|gingham|houndstooth"),
]

# (region, classes, extra tag regex or None); a material may land in several regions
REGIONS = [
    ("clock_town", {"plaster", "roof_tiles", "thatch", "cobble", "brick", "flagstone", "wood_planks", "wood_beam"}, None),
    ("termina_field", {"grass", "dirt", "rock", "gravel"}, None),
    ("swamp_woodfall", {"mud", "moss", "bark", "wood_beam", "wood_planks", "worked_stone", "dirt", "thatch"}, r"wet|moss|mud|dark|damp|mossy|green|swamp|rot|bark|forest|old|weathered|thatch"),
    ("snowhead", {"ice_snow", "rock", "wood_planks", "wood_beam", "worked_stone"}, r"snow|ice|frost|cold|grey|gray|cliff|slate|white|dark|old"),
    ("great_bay", {"sand", "gravel", "rock", "wood_planks", "wood_beam", "rope", "worked_stone", "thatch"}, r"thatch|sand|wet|beach|pebble|coral|weathered|rope|salt|sea|old|bleach|driftwood"),
    ("ikana_tower", {"sand", "rock", "worked_stone", "flagstone", "dirt", "brick"}, r"sand|dry|cracked|beige|yellow|ancient|carved|ruin|desert|old|medieval|ashlar|worn|weathered|sandstone|limestone"),
    ("interiors", {"plaster", "wallpaper", "fabric", "carpet_rug", "leather", "wood_planks", "wood_beam"}, None),
    ("dungeon", {"worked_stone", "brick", "flagstone", "metal", "cobble", "rock"}, r"dark|old|medieval|grate|rust|stone|worn|ancient|dirty|damaged|cracked|mesh|metal"),
]

GOOD = r"old|weather|worn|damag|rough|medieval|uneven|rustic|dirty|crack|chip|broken|aged|ancient|rust|mossy|cobblestone|rugged|ruin|hand|carved|scratch"
BAD = r"clean|modern|smooth|industrial|shiny|glossy|urban|office|polished|new\b|sleek|pristine|minimal|plain|simple|veneer|laminate|marble|onyx|climbing|anti.?slip|slab|bathroom|ceramic|vinyl|synthetic|speckle|embedded|chipboard|chips"


def classify(m):
    text = " ".join([m["name"], m["cat"], " ".join(m["tags"])]).lower()
    # name+category are reliable, tags are noisy ("dirty" metal): try them first, fall back to tags
    strong = " ".join([m["name"], m["cat"]]).lower()
    cls = next((c for c, rx in CLASSES if re.search(rx, strong)), None) or next((c for c, rx in CLASSES if re.search(rx, text)), None)
    if cls in (None, "skip"):
        return None
    regions = [r for r, cl, rx in REGIONS if cls in cl and (rx is None or re.search(rx, text))]
    if not regions:
        return None
    score = 5.0
    score += min(3, len(set(re.findall(GOOD, text)))) * 0.8 - min(3, len(set(re.findall(BAD, text)))) * 1.0
    score += {"photo": 0.8, "PBRPhotogrammetry": 0.8, "pbr-scanned": 0.8, "PBRProcedural": 0, "procedural": -0.5,
              "pbr-approximated": 0, "pbr-procedural": -0.3}.get(m["method"], 0)
    m = dict(m, cls=cls, regions=regions, auto=round(max(0, min(10, score)), 1))
    return m


if __name__ == "__main__":
    w = Path(sys.argv[1])
    out = []
    for f in sorted((w / "index").glob("*.json")):
        out += [c for c in map(classify, json.load(open(f))) if c]
    json.dump(out, open(w / "classified.json", "w"), indent=0)
    from collections import Counter
    print(len(out), Counter(c["cls"] for c in out).most_common())
    print(Counter(r for c in out for r in c["regions"]).most_common())
