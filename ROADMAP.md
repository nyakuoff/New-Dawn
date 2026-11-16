# New Dawn roadmap

New Dawn's goal is a *Majora's Mask* that looks like a current-generation remake while playing exactly like the
original. The art direction follows the 2026 *Ocarina of Time* remake: a hyper-detailed, tactile, practical-set world
that is dark and beautiful, with colourful doll-like characters (see [docs/DECISIONS.md](docs/DECISIONS.md) ND-014).
We upgrade the original look rather than match it.

This file tracks what's done, what's in progress and what's planned. The detail for each finished feature
(settings, cost, how it was verified) is in [docs/STATE.md](docs/STATE.md) and the per-feature notes in
[docs/](docs/). PR numbers refer to this repository; renderer changes have a matching PR on
[New-Dawn-libultraship](https://github.com/nyakuoff/New-Dawn-libultraship).

Status: ✅ done (in the New Dawn looks unless noted) · 🚧 in progress · 📋 planned · 🧪 experimental / opt-in

## Phase 1: Foundations ✅
| Feature | PRs |
|---|---|
| Post-processing framework: SSAO, bloom, grade, dither | #7 |
| Mipmaps, 16× anisotropic filtering, texture cache budget | #10, #14 |
| Async HD texture loading | #16, #19 |
| Per-pixel lighting | #15 |
| 4× MSAA with alpha-to-coverage for cut-out foliage | #25 |
| Capture and comparison tooling (`tools/newdawn/capture.py`) | #8, #40 |
| Temporal anti-aliasing with actor and grass masks ([docs/TAA.md](docs/TAA.md)) | #101 |

## Phase 2: HDR and colour ✅
| Feature | PRs |
|---|---|
| Linear HDR world, float MSAA, linear AO, shadows and bloom | #30, #32 |
| Hue-preserving tonemap and modern grade | #38 |
| Art-directed grade per area and time of day | #51 |
| Deep darks, darkening in cutscenes, tinted fairy light | #93 |

## Phase 3: Sky ✅
| Feature | PRs |
|---|---|
| Physical sky with volumetric clouds, driven by the game clock | #42 |
| Warm dawn and dusk palette; sky-driven ambient light | #43 |
| Sky reflections on water and glossy materials; overcast snow sky | #45 |
| Textured lens flare replacing the native sun wash | #49 |
| Screen-space sun rays through trees | #52 |
| Special skies: an apocalyptic final day, cursed Ikana, poisoned swamp, cutscene skies | #63 |
| Night sky: a real star field, a Milky Way panorama, a Moon glow and moonlight ([docs/NIGHT_SKY.md](docs/NIGHT_SKY.md)) | #96 |

## Phase 4: Lighting and shadows ✅
| Feature | PRs |
|---|---|
| Real-time sun shadows; cascades out to about 7,000 units; foliage and grass cut-out shadows | #21, #23, #24, #44, #59 |
| Shadows from room parts the game culls; soft cube-map shadows for the nearest flames | #102 |
| World-space global illumination: voxelised rooms and a probe grid, multi-bounce ([docs/WORLD_GI.md](docs/WORLD_GI.md)) | #104 |
| Screen-space bounce light kept as near-field detail | #53 |
| Dynamic local lights with darker nights and moody interiors; real flame light pools | #56, #60, #106 |
| Character rim light and specular, soft shading on low-poly characters, shiny metal | #46, #90 |
| Smooth character geometry: render-time curved surfaces, animations untouched ([docs/SMOOTH_CHARACTERS.md](docs/SMOOTH_CHARACTERS.md)) | #109 |
| Volumetric fog and haze, dungeon depth fog | #47, #49 |
| Interior emissives (clock face, signs); optional window light shafts (off by default) | #103 |

## Phase 5: Materials and textures ✅ / 🚧
| Feature | Status | PRs |
|---|---|---|
| Parallax occlusion mapping: real depth on brick, cobble and stone | ✅ | #57 |
| **Remastered textures:** about 2,300 world textures replaced with art-directed CC0 practical-set materials (84 sets, region variants) generated on your machine from your own game; unique painted art upscaled ([docs/TEXTURE_PIPELINE.md](docs/TEXTURE_PIPELINE.md)) | ✅ | #117, #118, #120, #122, #124 |
| Material maps and relief on interiors and dungeons | ✅ | #122, #124 |
| Objects and props (chests, pots, signs, fences, doors); more unique art from your 3DS copy | 🚧 | |

## Phase 6: Water ✅
| Feature | PRs |
|---|---|
| Fresnel, sun glints, screen-space reflections, refraction, per-area colour | #26, #29, #48 |
| Caustics on seabeds and an underwater view | #61 |
| Puddles, imperfect reflections, splashes above the surface | #88 |
| Ocean swells, ripple simulation around Link, natural colours ([docs/WATER_SIM.md](docs/WATER_SIM.md)) | #98 |
| Flowing rivers; natural shoreline foam; the Mountain Village waterfall | #107, #110 |

## Phase 7: Atmosphere, vegetation and effects ✅
| Feature | PRs |
|---|---|
| Ambient particles, rain and snow, wet surfaces, shared wind | #50 |
| Render-only 3D grass; leaf translucency, grass bending around Link, flowers and clover | #64, #89, #105 |
| Fire, embers, glows, dust and sparks; candles, bombs, run dust | #54, #58 |
| Magic arrows, a restyled sword trail | #65, #92 |
| Sparkle cores, frost, Deku bubble pop, mask transformation flash (lightning, hookshot kept native) | #108 |

## Phase 8: *Majora's Mask 3D* content (from your own 3DS dump) 🧪
Optional: give the game your own *Majora's Mask 3D* dump and New Dawn converts its models on your machine. Nothing
from the 3DS game is ever shipped.

| Feature | Status | PRs |
|---|---|---|
| 3DS room geometry in 18 scenes (clean-room converter) | 🧪 | #36, #37, #41 |
| 3DS character models on the original skeletons and animations; textures upscaled; blinking and talking faces | 🧪 | #113, #116, #119, #121 |
| Link's five forms | 🧪 behind "Include unreviewed 3DS models" | #123 |
| A reference-vs-converted fidelity gate for every model; modern character lighting | 🚧 | |
| 3DS collision trial (South Clock Town) | 🧪 not shipped ([docs/MM3D_COLLISION_TRIAL.md](docs/MM3D_COLLISION_TRIAL.md)) | #41 |

## Phase 9: Menu, options and performance ✅
| Feature | PRs |
|---|---|
| New Dawn menu with five looks (Ultra, High, Medium, Low, Original look) ([docs/MENU.md](docs/MENU.md)) | #86 |
| Mouse Look, Warp tab, Dev Tools page, performance overlay (F3) | #87, #91, #95 |
| Whole-frame performance profile and rebalanced looks ([docs/PERFORMANCE.md](docs/PERFORMANCE.md)): Ultra 4.6 ms on an RX 7800 XT | #114 |
| Every shipped asset committed with its licence ([docs/licenses/THIRD_PARTY_ASSETS.md](docs/licenses/THIRD_PARTY_ASSETS.md)) | #97, #99 |

## Planned 📋
Roughly in priority order; this will change as work lands.

1. **Character materials** (done, local: `docs/CHARACTER_LIGHTING.md`). Skin that transmits light, cloth and felt sheen, metal, hair highlights, for both the
   original and the 3DS models (after the character lighting pass now in progress).
2. **Objects and props** in the texture pipeline (in progress), and more unique art sourced from your 3DS copy.
3. **Temporal upscaling.** Render the world below native resolution and upsample with the TAA history.
4. **Other backends.** DirectX 11 and Metal ports of the OpenGL-only effects (Windows and macOS players currently need OpenGL).
5. **HDR display output.** True HDR10/scRGB output for HDR monitors, off by default and opt-in.
6. **Releases.** Packaged builds once the look settles; a target download of about 150–250 MB, with everything
   game-derived generated on your machine.

## Out of scope
- Changes to gameplay, physics, timing, AI, schedules, cutscene sequencing, saves or animation data. Cutscene
  *visuals* are in scope.
- Redistributing ROMs, game assets, decryption keys or third-party mods without redistribution rights, or shipping
  art that depicts Nintendo's characters or places (ND-013).
