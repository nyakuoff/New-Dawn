# New Dawn roadmap

New Dawn's goal is a *Majora's Mask* that looks like a current-generation remake while playing exactly like the
original. This file tracks what's done, what's in progress and what's planned. The detail for each finished feature
(settings, cost, how it was verified) is in [docs/STATE.md](docs/STATE.md) and the per-feature notes in
[docs/](docs/). PR numbers refer to this repository; renderer changes have a matching PR on
[New-Dawn-libultraship](https://github.com/nyakuoff/New-Dawn-libultraship).

Status: ✅ done (in the New Dawn preset unless noted) · 🚧 in progress · 📋 planned

## Phase 1: Foundations ✅
| Feature | PRs |
|---|---|
| Post-processing framework: SSAO, bloom, grade, dither | #7 |
| Mipmaps, 16× anisotropic filtering, texture cache budget | #10, #14 |
| Async HD texture loading | #16, #19 |
| Per-pixel lighting | #15 |
| 4× MSAA with alpha-to-coverage for cut-out foliage | #25 |
| New Dawn preset and "Original look" restore | #17 |
| Capture and comparison tooling (`tools/newdawn/capture.py`) | #8, #40 |

## Phase 2: HDR and colour ✅
| Feature | PRs |
|---|---|
| Linear HDR world, float MSAA, linear AO, shadows and bloom | #30, #32 |
| Hue-preserving tonemap and modern grade | #38 |
| Art-directed grade per area and time of day | #51 |
| Offline texture recolour for muddy or garish textures | #51 |

## Phase 3: Sky ✅
| Feature | Status | PRs |
|---|---|---|
| Physical sky with volumetric clouds, driven by the game clock | ✅ | #42 |
| Warm dawn and dusk palette; sky-driven ambient light | ✅ | #43 |
| Sky reflections on water and glossy materials; overcast snow sky | ✅ | #45 |
| Textured lens flare replacing the native sun wash | ✅ | #49 |
| Screen-space sun rays through trees | ✅ | #52 |
| Special skies: an apocalyptic final day, cursed Ikana, poisoned swamp, and upgraded cutscene skies | ✅ | #63 |

## Phase 4: Lighting and shadows ✅ / 🚧
| Feature | Status | PRs |
|---|---|---|
| Real-time sun shadows with a Poisson PCF filter | ✅ | #21, #23, #24 |
| Foliage, fence and grass cut-out shadows | ✅ | #44 |
| Cascaded shadows out to about 7,000 units | ✅ | #59 |
| Character rim light and specular | ✅ | #46 |
| Screen-space bounce light (GI) with colour bleeding | ✅ | #53 |
| Dynamic local lights (torches, lamps, flames) with darker nights and moody interiors | ✅ | #56, #60 |
| Volumetric fog and haze, dungeon depth fog | ✅ | #47, #49 |

## Phase 5: Materials ✅
| Feature | PRs |
|---|---|
| Generated normal, roughness and height maps for about 2,200 world textures | #31, #33, #34 |
| Parallax occlusion mapping: real depth on brick, cobble and stone | #57 |

## Phase 6: Water ✅ / 📋
| Feature | Status | PRs |
|---|---|---|
| Fresnel, sun glints and screen-space reflections | ✅ | #26, #29 |
| Per-area art-directed water colour, shoreline foam, refraction, sharper reflections | ✅ | #48 |
| Caustics on seabeds and an underwater view (fog, light shafts, particles) | ✅ | #61 |
| **Water simulation:** real ocean waves (Gerstner or FFT displaced surface) with crest foam and shoreline swash; interactive ripples and wakes from Link, actors and splashes; better rivers and waterfalls. Render-only: the game's water height stays authoritative for swimming and collision. | 📋 | |

## Phase 7: Atmosphere and effects ✅
| Feature | Status | PRs |
|---|---|---|
| Ambient particles: sunlit dust motes, pollen, fireflies, drifting leaves | ✅ | #50 |
| Rain, snow and wet surfaces; roof shelter | ✅ | #50 |
| Shared wind with render-only foliage sway | ✅ | #50 |
| Fire, embers, glows, dust and sparks from real assets | ✅ | #54 |
| Candles, fireplace, bomb explosions, run dust | ✅ | #58 |
| Render-only 3D grass on grassy ground | ✅ | #64 |
| Fire, ice and light arrows (HDR trails and impacts) | ✅ | #65 |
| Fairy sparkles, rupee glints, hookshot, Deku bubble and mask flashes | 📋 (sparkles kept native for now) | |

## Phase 8: Geometry 🧪
| Feature | Status | PRs |
|---|---|---|
| *Majora's Mask 3D* room geometry (18 scenes, clean-room converter from your own dump), opt-in | ✅ experimental | #36, #37, #41 |
| MM3D animated materials and highlight roll-off | ✅ | #37 |
| MM3D collision trial (South Clock Town) | 🧪 not shipped: 9.6% of walkable floor would let Link fall through; see [docs/MM3D_COLLISION_TRIAL.md](docs/MM3D_COLLISION_TRIAL.md) | #41 |

## Phase 9: Menu and first run ✅
| Feature | PRs |
|---|---|
| New Dawn menu: themed pages and tabs, search, wordmark and Cinzel headings; every 2Ship setting kept (`docs/MENU.md`) | #86 |
| Five looks (Ultra, High, Medium, Low, Original look), Ultra applied on a fresh install, one-time offer to existing configs | #86 |
| Mouse Look toggle: mouse free look and first-person/bow aiming | #87 |
| Warp tab: one-click teleports to key locations, plus saved warp points | #91 |

## Playtest fixes ✅
The first full manual playtest led to fixes for washed-out darks and the opening (#93), character lighting, flicker and shadow motion (#90), grass placement and sway (#89), water puddles, reflections and splashes (#88), and the arrow and sword-trail effects (#92).

## Planned 📋
Roughly in priority order; this will change as work lands.

1. **Temporal anti-aliasing / upscaling.** A TAA or temporal upsampler to remove shimmer on fine detail (grass, POM edges, foliage) and give headroom for the heavier effects.
2. **Character and actor materials.** Proper normal and specular detail for Link, NPCs and enemies (the 3DS Link mod plus generated maps), skin/cloth/metal response, and better hair.
3. **Water simulation** (Phase 6 above): waves, interaction, rivers and waterfalls.
4. **Shadow coverage.** Shadows from geometry the game culls (rooms behind the camera, distant actors), and point-light shadows for the nearest flames.
5. **Foliage and vegetation.** Denser trees and bushes, translucency (light through leaves), and grass interaction around Link.
6. **Night sky** (done locally on `feature/night-sky`, `docs/NIGHT_SKY.md`). A real star field (13,964 catalogue stars), a Milky Way panorama and a Moon glow that follows the giant Moon's state, plus soft moonlight shadows.
7. **Interior polish.** Light leaking through windows and doorways, dust in light beams, more emissive surfaces (signs, lamps, crystals).
8. **Performance and options.** Quality tiers (low/medium/high/ultra), a performance overlay, and a pass to keep the full preset comfortably above 60 fps at 1080p on mid-range GPUs.
9. **Other backends.** DirectX 11 and Metal ports of the OpenGL-only effects (Windows and macOS players currently need OpenGL).
10. **HDR display output.** True HDR10/scRGB output for HDR monitors, off by default and opt-in by the player (the existing "HDR" is the internal lighting pipeline, which outputs normal SDR).
11. **Releases.** Packaged builds once the feature set stabilises.

## Out of scope
- Changes to gameplay, physics, timing, AI, schedules, cutscene sequencing, saves or animation data. Cutscene
  *visuals* are in scope.
- Redistributing ROMs, game assets, decryption keys or third-party mods without redistribution rights.
