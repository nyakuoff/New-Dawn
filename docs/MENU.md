# The New Dawn menu

Press **Esc** (or the pad's menu button when *Menu Controller Navigation* is on) to open the menu. It replaces the
2 Ship 2 Harkinian settings menu: a left rail of pages, a tab row per page, and a search box (finds any setting,
including its tooltip). `F1` still toggles the small top bar; `F11`, `Tab` (alternate assets) and `Ctrl+R` are unchanged.

Source: `mm/2s2h/BenGui/` — `Menu.cpp` (layout), `NewDawnTheme.cpp` (palette, style, rail/tab widgets),
`NewDawnPresets.cpp` (look tiers, first-run), `BenMenu.cpp` (every setting and the page map in `AddPages()`).
Widgets are still registered with `AddWidget(path, ...)` against an (entry, sidebar) pair, so other modules that
add settings keep working; a *page* is a list of *tabs*, each tab showing one sidebar.

## Pages

| Page | Tabs (sidebar shown) |
|---|---|
| Graphics | **Quality** (look cards + the settings that matter most), **Advanced** (every `gNewDawn.*` setting under collapsible groups: Sky, Lighting & Shadows, Atmosphere, Water, Materials & Textures, Effects & Foliage, Image & Colour, Experimental), Extras (clock, motion blur, 3D drops, N64 mode...), Mods (texture/model pack list) |
| Display | Window and resolution: fullscreen, vsync, renderer, widescreen / advanced resolution, texture filter |
| Audio | Volume sliders, audio API; the Audio Editor |
| Controls | Bindings (the input editor), Camera, Menu and hotkeys |
| Gameplay | Quality of Life, Items and Songs, Time Savers, Fixes, Restorations, Difficulty |
| Cheats | all cheats |
| Interface | Menu look, HUD editor, cosmetics, notifications and timer, input viewer, time splits, config presets |
| Accessibility | menu text size, screen flash / quake / distortion options, bow reticle, shooting gallery markers |
| Randomizer | the 2Ship randomizer settings |
| About | credits (including the required attribution for the bundled sky, flare and particle art), version, **Developer mode** switch |
| Developer *(hidden)* | stats, console, collision/actor/event/DL/message viewers, save editor, hook and gfx debuggers, warp point. Shown only when **About > Developer mode** is on |

Nothing was deleted: the one-time "Search In Sidebar" / "Search autofocus" options were dropped because the new menu
always has its search box in the header; everything else is in a page above.

## Looks (Graphics > Quality)

Five cards. Picking one writes every cvar below and saves the config. The card that matches the current cvars exactly
is marked ACTIVE; anything else reads "Custom". Rows not listed (strengths, radii...) are identical in the four New Dawn
tiers; the full table is `kRows` in `NewDawnPresets.cpp` (`tools/newdawn/capture.py --preset` reads the same table).

| cvar | Ultra | High | Medium | Low | Original look |
|---|---|---|---|---|---|
| `gSettings.MSAAValue` | 4 | 4 | 2 | 1 | 1 |
| `gNewDawn.Textures.Anisotropy` | 16 | 16 | 8 | 4 | 16 |
| `gNewDawn.Textures.AlphaToCoverage` | 1 | 1 | 1 | 0 | 1 |
| `gNewDawn.GI.Enabled` (bounce light) | 1 | 0 | 0 | 0 | 0 |
| `gNewDawn.Fog.Volumetric` | 1 | 1 | 0 | 0 | 0 |
| `gNewDawn.Shadows.MapSize` | 4096 | 2048 | 2048 | 1024 | 4096 |
| `gNewDawn.Shadows.Cascades` | 4 | 3 | 2 | 1 | 1 |
| `gNewDawn.Shadows.Foliage` | 1 | 1 | 0 | 0 | 0 |
| `gNewDawn.Grass.Density` | 1.0 | 0.8 | 0.5 | 0.3 | 1.0 |
| `gNewDawn.Grass.Distance` | 1000 | 850 | 650 | 450 | 1000 |
| `gNewDawn.Water.Reflections` | 1 | 1 | 0 | 0 | 0 |
| `gNewDawn.Water.Caustics` | 1 | 1 | 0 | 0 | 0 |
| `gNewDawn.Water.Foam` / `Refraction` / `Underwater` | 1 | 1 | 1 | 0 | 0 |
| `gNewDawn.Sky.Clouds` | 1 | 1 | 1 | 0 | 0 |
| `gNewDawn.Sky.Reflections` | 1 | 1 | 0 | 0 | 0 |
| `gNewDawn.Sky.Flare` | 1 | 1 | 1 | 0 | 0 |
| `gNewDawn.Materials.Parallax` | 1 | 1 | 0 | 0 | 0 |
| `gNewDawn.PostFx.AO.Enabled` | 1 | 1 | 1 | 0 | 1 |
| `gNewDawn.SunShafts.Enabled` | 1 | 1 | 1 | 0 | 0 |
| `gNewDawn.Atmos.Particles` | 1 | 1 | 1 | 0 | 0 |
| `gNewDawn.Lighting.Characters` | 1 | 1 | 1 | 0 | 0 |
| `gNewDawn.Lighting.PointLightQuality` | 0.5 | 0.5 | 0.5 | 0.25 | 0 |

Every tier except Original look keeps the modern lighting pipeline (`gNewDawn.HDR.Enabled`: linear lighting mapped to normal SDR output, no HDR monitor needed; the menu never calls it "HDR"), the physical sky, per-pixel lighting, local lights, materials, the colour
grade, wind, weather, VFX and 3D grass on. **Original look** turns every New Dawn effect off (including scene
post-processing and dither), 1x MSAA and no interpolation to the display's refresh rate: the N64 path that plain
2Ship renders. The cloud march has no step-count setting yet, so Low only switches clouds off.

The looks touch graphics settings only: never cheats, HUD, widescreen, timing, gameplay options or asset mods
(`gEnhancements.Mods.AlternateAssets` stays off until the player turns it on; the look works, and degrades
gracefully, without any texture or model packs, and without the locally generated material, recolour and effect
assets).

## First run and existing configs

`gNewDawn.Setup.Version` in `2ship2harkinian.json` records that setup has run.

* **No config file** (a fresh install): the Ultra look is applied, controller navigation of the menu is switched on, and a
  welcome notification appears. Nothing else is changed.
* **An existing config without the version** (plain 2Ship, or an older New Dawn): nothing is changed. A one-time
  popup offers *Apply the New Dawn look* or *Keep my settings*. A config that already has HDR on is treated as set up.
* After that the choice just persists like any other setting; picking *Original look* later is remembered and the
  popup never returns. Config keys are unchanged, so a config also loads in an older build.

## Notes for developers

* New settings: `AddWidget(path, "Label", WIDGET_CVAR_CHECKBOX)` as before. Set `path.group = "Sky";` for the
  Advanced tab (groups are drawn in the order of `groupOrder` in `AddSettings()`), and `.HideInSearch(true)` on a
  deliberate duplicate (the Quality tab repeats a few Advanced settings).
* A page is added in `BenMenu::AddPages()`. Tabs render one sidebar; `plain` sidebars skip the section cards.
* The menu opens at launch with `gWindows.Menu=1`; `gSettings.Menu.ActivePage` (page id) and
  `gSettings.Menu.Tab.<page id>` (tab index) choose the page, which is how the screenshots are captured.

## Dev Tools and performance overlay

* Dev Tools is a normal rail page (after About); the old Developer-mode gate is gone.
* F3 toggles the performance overlay (`gNewDawn.Perf.Overlay`, `.Corner` 0-3, `.Detailed`). The overlay cvar also enables the GPU timer queries in libultraship (`fast/newdawn_perf.h`); off = no queries. Per-stage values are smoothed and keep their last value if a stage stops running. VRAM needs GL_NVX_gpu_memory_info (or shows free-only with GL_ATI_meminfo); RAM/CPU readouts are Linux only.
