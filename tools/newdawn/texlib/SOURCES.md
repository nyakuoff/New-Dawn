# Texture sources: licence verdicts (checked 2026-11-16)

Hard rule (maintainer): a source is used only if its **own terms explicitly allow redistribution of the texture files**,
inside a public repository and in the shipped game. When in doubt, it is excluded. Snapshots of each primary licence/terms
page are in `docs/licenses/texlib/` (excluded sources keep a snapshot as evidence, prefixed `EXCLUDED-`).

| Source | Verdict | Licence evidence (primary page) | Notes |
|---|---|---|---|
| ambientCG | ✅ shippable | CC0 1.0, https://docs.ambientcg.com/license/ ("copy, modify, distribute ... include the raw files in your project, for example a video game") | JSON API, 2K-JPG zips. Main source. |
| Poly Haven | ✅ shippable | CC0, https://polyhaven.com/license ("All assets ... licensed as CC0") | API gives per-map 2K JPGs. Main source. |
| TextureCan | ✅ shippable | CC0 1.0, https://www.texturecan.com/terms/ ("allowed to be redistributed together with your projects") | Many are Substance-procedural, mostly clean/modern; few used. |
| AMD GPUOpen MaterialX Library | ✅ shippable (MIT, **notice required**) | API licence `MIT` for all 454 materials (author AMD), text in `docs/licenses/texlib/gpuopen-mit.txt`; MIT explicitly grants "distribute, sublicense" | Fabric/wallpaper/tile style, mostly clean. The MIT copyright notice must ship with any used material (see `docs/licenses/TEXLIB_CREDITS.md`). |
| cgbookcase | ✅ licence CC0, ⚠️ **manual download only** | CC0 1.0 stated on https://www.cgbookcase.com/textures ("published under the CC0 1.0 license") | Their downloads sit behind a hotlink-protected CDN (403 without their page's Referer). We do not work around that. Indexed (582) but not scored/shortlisted; the maintainer can download by hand and drop files in (see README). |
| 3dtextures.me | ✅ licence CC0, ⚠️ **manual download only** | "All textures on this site are licensed as CC0" (F.A.Q./Textures License, https://3dtextures.me/about/) | Per-asset download buttons; no scriptable URL. Not indexed. Drop-in only. |
| BlenderKit / Blendkit | ⚠️ only items tagged CC0 | https://www.blendkit.com/docs/licenses/ : two licences. "Royalty Free" forbids re-selling assets "in the same form (e.g. ... part of an assetpack or game level)" so it is **excluded**; items individually marked CC0 qualify. | Needs login/API; CC0 items are mostly mirrors of ambientCG/cgbookcase, so nothing was added. |
| Texture.ninja | ❌ | Only an in-app "public domain (CC0)" badge; no licence/terms page; mostly photographs (e.g. temple photos) of unclear provenance, 450 px | Excluded: no primary licence page and unclear provenance. |
| Poliigon (free) | ❌ | https://help.poliigon.com/en/articles/8749749-asset-use-licensing : "You can't share, sell, license, redistribute or repackage our assets"; "can't use our assets in ... games where the users have direct or indirect access to the assets" | Excluded, no explicit permission. |
| FreePBR | ❌ | https://freepbr.com/about-free-pbr/ : free for non-commercial use only; commercial use needs a paid $19 licence; "don't redistribute these PBR file sets" | Excluded entirely (maintainer decision). |
| colormass library | ❌ | https://www.colormass.com/legal/colormass-3d-texture-library : may not "distribute ... as standalone files", may not "distribute the software in a way that allows end-users to easily extract the raw texture files", no open repositories, no AI training | Excluded. |
| Adobe Substance 3D Community Assets | ❌ | Custom "Community Assets License 1.1" (the page returns 403 to automated fetch; only the Product Specific Terms PDF could be read), not CC0, per-asset licence; .sbsar needs Substance to bake | Excluded: no explicit redistribution grant verified. Maintainer can opt in per asset only if an asset page says CC0/CC-BY. |
| PolyScan / Polyscann | ❌ | https://polyscann.com/license is titled "CC0 License Terms" but adds "can't ... redistribute ... assets", "no mass downloading ... scripts or bots", "don't upload to other platforms" | Excluded: contradicts CC0. |
| ShareTextures | ❌ | https://www.sharetextures.com/p/license : "custom CC0 ... with specific restrictions": no redistribution in plugins/collections, no automated downloads, no use in "violent" content | Excluded. |
| LotPixel Hub | ❌ | Credit/subscription marketplace ("credits per month"), no free redistribution licence found | Excluded. |
| CGAxis (free textures) | ❌ | Own "TOS & License"; not CC0; its free assets are sample content under CGAxis terms | Excluded. |
| Kimbatt's cc0-textures | ➖ not a source | https://github.com/Kimbatt/cc0-textures : a torrent mirror of ambientCG/Poly Haven-style CC0 sets "not made by me". Redundant with the primary sources, so we go to the primaries (clearer provenance). | |
| OpenGameArt / Substance Source / textures.com | ❌ not used | Per-asset or custom licences (OGA mixes CC-BY/CC-BY-SA/GPL/CC0; textures.com forbids redistribution) | Only individually CC0 OGA items would qualify; none needed. |

Not checked yet / ideas: Sketchfab CC0 textures, Freestylized (CC0, mostly stylised), Wikimedia Commons PD photographs (not PBR).
