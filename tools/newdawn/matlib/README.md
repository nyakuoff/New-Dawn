# Base material library tools (ND-016)

* `library.json`: the selection (id, ambientCG asset, class, roles, region). Data-driven: add an entry, rerun the build.
* `build_library.py`: downloads the 2K ambientCG zips (untrusted data: `--dl` points to its own folder), re-encodes albedo, normal (GL), roughness and height to
  1024² JPEG, and writes `manifest.json` (class, roles, region, source URL, licence, zip SHA-256) and `MANIFEST.md` into `mm/assets/newdawn/materials/`.
  It aborts if the ambientCG licence page no longer says "CC0 1.0 Universal". Run with `python3 -I`.
* `synth_cli.cpp`: standalone driver of `mm/2s2h/NewDawn/TextureSynth.cpp` for tuning without the game (see its header comment).

The curated multi-source catalog (`tools/newdawn/texlib/`) is the pool the library grows from; its shortlist is not imported yet (see `docs/TEXTURE_PIPELINE.md`).
