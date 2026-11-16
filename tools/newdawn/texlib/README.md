# New Dawn texture library: catalog, triage and drop-in process (ND-016)

This directory holds the **source catalog** of redistributable PBR materials New Dawn may ship as its base library.
It contains URLs, licences, hashes, classes, regions and art-direction scores. **No image files are committed here**;
the engine picks the final set from `catalog.json` and compresses it (target <= 80 MB for the base library).

| File | Purpose |
|---|---|
| `SOURCES.md` | Licence verdict per source, with the primary licence page. Read before adding a source. |
| `catalog.json` | One entry per scored material: `id, source, name, page, license, license_page, license_doc, download, files[{name,url,bytes,sha256}], sha256, class, regions, score, tier, method`. Tiers: `shortlist` (>= 8, ~100) > `candidate` (>= 7.5) > `reserve` (6-7.5) > `rejected` (< 6). |
| `triage.txt` | The art-direction scores (`id score [class]`), made by looking at contact sheets. The source of truth for `score`/`class`. |
| `index.py` | Pulls the public catalogs (ambientCG, Poly Haven, GPUOpen, TextureCan, cgbookcase) into `WORKDIR/index/`. |
| `classify.py` | Keyword classes (brick, cobble, wood_planks, ...) and Termina regions, plus a pre-score. |
| `sheets.py` | Downloads preview thumbnails and builds per-class contact sheets (montage step goes through `nd-heavy tool`). |
| `build_catalog.py` | `triage.txt` + indexes -> `catalog.json` (tiers, shortlist rules). Keeps already-pinned hashes. |
| `fetch.py` | Downloads the 2K sources of a tier into `WORKDIR/sets/<source>_<id>/` and pins SHA-256 in `catalog.json`. |
| `credits.py` | Regenerates `docs/licenses/TEXLIB_CREDITS.md` for every shortlisted asset. |

Reproduce: `WORK=~/dev/ghm-mods/work/texlib` then
`python3 -I index.py $WORK && python3 -I classify.py $WORK && python3 -I build_catalog.py $WORK && python3 -I fetch.py $WORK shortlist && python3 -I credits.py`.
Downloads are untrusted data: keep them in `$WORK`, never in the repo, and run Python with `-I` on anything that reads them.

## Adding a texture you found yourself (drop-in)
1. **Check the licence first** on the site's *primary* licence/terms page (not a forum post, not a mirror). Save its text
   to `docs/licenses/texlib/<site>.txt` (first lines: source URL + date). Checklist, all must be yes:
   - [ ] CC0 / public domain, or MIT/CC-BY/CC-BY-SA (then attribution is added to `TEXLIB_CREDITS.md`)?
   - [ ] The text **explicitly** allows redistribution of the files (public repo and inside the shipped game archive)?
   - [ ] No NC, no "personal use only", no "don't redistribute the raw files", no "users may not be able to extract", no AI/ML clause, no login-gated terms?
   - [ ] The asset is original work of the site (not a mirror of someone else's licence)? Photos of buildings with signage/logos/people are out.
   - [ ] Resolution is 2K (4K+ is wasted download); PBR set with colour + normal + roughness (+ AO/height if available)?
   - If any box is "unsure": it is excluded. Record why in `SOURCES.md`.
2. **Put the files** in `$WORK/sets/<Source>_<id>/` (any file names; keep the originals).
3. **Add one line to `triage.txt`**: `<id> <score 0-10> [class]` after looking at it (tactile, handcrafted, dark and
   beautiful per ND-014, reads well when recoloured; < 6 = no). For a source not yet in the indexes, also add an entry to
   the matching `index.py` function (or a small `index/manual.json` with the fields used by `build_catalog.py`: `source,id,name,page,cat,tags,method,thumb,dl,bytes,lic`)
   and, if the licence is new, a `LIC_PAGE`/`LIC_DOC` entry in `build_catalog.py`.
4. `python3 -I build_catalog.py $WORK && python3 -I fetch.py $WORK shortlist && python3 -I credits.py`
   (`fetch.py` hashes manual drop-ins in place), then commit `catalog.json`, `triage.txt` and `docs/licenses/`.
