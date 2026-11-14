<p align="center"><img src="docs/images/new-dawn-wordmark.png" width="600"></p>

<p align="center"><b>A modern visual remaster of <i>The Legend of Zelda: Majora's Mask</i>, built on 2 Ship 2 Harkinian.</b></p>

**New Dawn** makes Termina look reborn, the way a current-generation remake would, while the game plays exactly
like the original. Gameplay, physics, timing, AI, schedules, cutscene sequencing and save data are untouched. Only
the rendering changes, and every effect can be switched off to get back the original code path.

New Dawn is a fork of [2 Ship 2 Harkinian](https://github.com/2ship2harkinian/2ship2harkinian) (2Ship), the PC port
of Majora's Mask, and of its renderer [libultraship](https://github.com/Kenix3/libultraship). See
[docs/NEW_DAWN.md](docs/NEW_DAWN.md) for the project's ground rules and layout, and
[ROADMAP.md](ROADMAP.md) for what's done and what's next.

> New Dawn includes no copyrighted game assets. You need your own supported copy of the game, exactly as with 2Ship.

## What New Dawn changes

Everything below is on in the **New Dawn preset** (**Esc** → Settings → Graphics → New Dawn → *Apply New Dawn
Preset*). Each feature has its own toggle in the same menu.

| Area | What you get |
|---|---|
| **HDR and colour** | A linear HDR pipeline with a hue-preserving tonemap and filmic grade, an art-directed colour grade per area and time of day, and an optional texture recolour pass for muddy or garish textures. |
| **Sky** | A physical sky with volumetric clouds that follow the in-game clock; warm dawns and dusks; special skies for the final day (an intensifying storm under the Moon), cursed Ikana and the poisoned swamp, also in cutscenes; and an overcast snow sky. Sky-driven ambient light and reflections keep the world consistent with the sky. |
| **Lighting** | Real-time sun shadows with 4 cascades out to about 7,000 units; shadows from foliage, fences and grass; screen-space bounce light (GI) with colour bleeding; per-pixel lighting with character rim and specular light; and dynamic flickering light from torches, lamps and flames, with darker, moodier nights and interiors. |
| **Grass** | Dense, render-only 3D grass on grassy ground that sways in the wind and is lit like the world (no collision change). |
| **Atmosphere** | Volumetric fog and haze, screen-space sun rays through trees, a textured lens flare, sunlit dust motes, pollen, fireflies and drifting leaves, plus a shared gusting wind that sways foliage and drives clouds and particles. |
| **Weather** | Soft sky-lit rain sheltered under roofs, splash rings, wet reflective ground that lingers after rain, and softer snow. |
| **Water** | Per-area art-directed water colour, sharp reflections of geometry and the sky, Fresnel and sun glints, shoreline foam, refraction with depth absorption, caustics on seabeds, and an underwater view with fog, light shafts and particles. |
| **Materials** | Generated normal, roughness and height maps for about 2,200 world textures, with GGX highlights and parallax occlusion mapping, so brick, cobble and stone have real depth. |
| **Effects** | Redrawn fire (an animated flipbook rendered from a fluid simulation) with embers and light, soft glows, dust, sparks, bomb explosions, candles, and fire/ice/light arrows. |
| **Image quality** | 4× MSAA with alpha-to-coverage for foliage edges, mipmaps with 16× anisotropic filtering, and async HD texture loading. |
| **3DS geometry (experimental)** | An optional switch that draws the *Majora's Mask 3D* room geometry in 18 scenes, converted from your own 3DS dump with a clean-room tool. Off by default and not part of the preset. |

The renderer work is OpenGL-only for now. On Windows, choose OpenGL in *Settings → Graphics backend*.

## Getting started

### 1. Build
New Dawn has no prebuilt releases yet. Build it the same way as 2Ship; see [docs/BUILDING.md](docs/BUILDING.md).
On Linux:

```sh
git clone --recursive https://github.com/nyakuoff/New-Dawn.git
cd New-Dawn
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
cmake --build build --target Generate2ShipOtr
```

The game is `build/mm/2s2h.elf`. On first launch it asks for your ROM and extracts the game data, exactly like
2Ship. You can check that your dump is supported at <https://2ship.equipment/> or against
[docs/supportedHashes.json](docs/supportedHashes.json).

### 2. Turn on the New Dawn look
Press **Esc** to open the settings menu, go to **Settings → Graphics → New Dawn**, and click **Apply New Dawn
Preset**. *Restore Defaults* returns those settings to the original look.

### 3. Optional: HD textures and models
New Dawn is built and tuned around these community mods. They can't be redistributed here, so download them
yourself, put the files in the game's `mods/` folder, and turn on **Use Alternate Assets** (**Tab**).

| Mod | Source |
|---|---|
| MM Reloaded 4K (2Ship O2R) | [evilgames.eu](https://evilgames.eu/texture-packs/mm-reloaded.htm) |
| 3DS Link and 3DS Equipment | [GameBanana 644553](https://gamebanana.com/mods/644553) |
| HD Moon | [GameBanana 640438](https://gamebanana.com/mods/640438) |

The material maps, texture recolour and effect textures are generated locally with the tools in
[`tools/newdawn/`](tools/newdawn/README.md); they aren't shipped either.

## Controls

These are 2Ship's defaults.

| N64 | A | B | Z | Start | Analog stick | C buttons | D-Pad |
| - | - | - | - | - | - | - | - |
| Keyboard | X | C | Z | Space | WASD | Arrow keys | TFGH |

| Keys | Action |
| - | - |
| Esc | Settings menu (New Dawn options are under Settings → Graphics) |
| F1 | Toggle the menu bar |
| F11 | Fullscreen |
| Tab | Toggle alternate assets |
| Ctrl+R | Reset |

## Documentation

| Document | Contents |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Finished, current and planned work |
| [docs/NEW_DAWN.md](docs/NEW_DAWN.md) | Ground rules, repository layout, workflow |
| [docs/STATE.md](docs/STATE.md) | Feature status, the settings behind each feature and how it was verified |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · [docs/DECISIONS.md](docs/DECISIONS.md) | How the renderer is extended, and why |
| [docs/VALIDATION.md](docs/VALIDATION.md) | How visual changes are tested |
| Feature notes | One file per feature in [docs/](docs/), e.g. [PHYSICAL_SKY](docs/PHYSICAL_SKY.md), [CASCADED_SHADOWS](docs/CASCADED_SHADOWS.md), [LOCAL_LIGHTS](docs/LOCAL_LIGHTS.md), [WATER_FOAM](docs/WATER_FOAM.md), [VFX](docs/VFX.md) |
| [tools/newdawn/](tools/newdawn/README.md) | Capture and comparison tooling, material and recolour generators |

## Contributing

`main` always builds. Work goes on `feature/*` or `fix/*` branches and lands through pull requests. A renderer
change is two pull requests: one into `new-dawn` on
[New-Dawn-libultraship](https://github.com/nyakuoff/New-Dawn-libultraship), merged first, then one here that
moves the submodule pointer. Visual changes need before/after captures, and switching a feature off must give
output identical to `main`. Details are in [docs/NEW_DAWN.md](docs/NEW_DAWN.md) and
[docs/VALIDATION.md](docs/VALIDATION.md).

## Credits and license

New Dawn stands on [2 Ship 2 Harkinian](https://github.com/2ship2harkinian/2ship2harkinian) by HarbourMasters and
its contributors ([Discord](https://discord.com/invite/sxrPyuh697)), the
[Majora's Mask decompilation](https://github.com/zeldaret/mm), and
[libultraship](https://github.com/Kenix3/libultraship). Please direct New Dawn issues to this repository, not to
2Ship. Third-party assets used locally are credited in their own manifests.

The code is released under [CC0 1.0](LICENSE), the same as 2Ship. *The Legend of Zelda: Majora's Mask* is © Nintendo;
New Dawn is not affiliated with or endorsed by Nintendo. We do not condone piracy.

<a href="https://github.com/Kenix3/libultraship/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./docs/poweredbylus.darkmode.png">
    <img alt="Powered by libultraship" src="./docs/poweredbylus.lightmode.png">
  </picture>
</a>
