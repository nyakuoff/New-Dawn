# New Dawn

New Dawn is a visual modernization of The Legend of Zelda: Majora's Mask, built on
[2 Ship 2 Harkinian](https://github.com/2ship2harkinian/2ship2harkinian). The aim is a game that looks like a
modern remake while playing exactly like the original.

## Ground rules

- **Visual only.** Nothing may change gameplay, physics, timing, animation, AI, schedules, cutscenes or anything
  else game logic observes. Every rendering change must leave what the game reads back (depth queries, framebuffer
  captures) as it was.
- **Keep the atmosphere.** Effects should enrich the original art direction, not replace it: no heavy bloom, no
  sharpening, no generic photorealism.
- **Every effect is a setting.** New rendering features can be switched off, and switching them off restores the
  original code path.

## Where things live

| Area | Location |
|---|---|
| Renderer (Fast3D interpreter, OpenGL backend, post-processing) | `libultraship/` submodule, fork [New-Dawn-libultraship](https://github.com/nyakuoff/New-Dawn-libultraship), branch `new-dawn` |
| Game-side hooks and settings | `mm/2s2h/Enhancements/Graphics/`, the "Green Hat's Mask" group in the Graphics menu |
| Test and capture tooling | `tools/ghm/` |

## Textures and models

Textures and models come from existing community mods, which are installed locally and never committed or
redistributed here (none of them grant redistribution rights). Put the `.o2r`/`.otr` files in the game's `mods/`
folder and enable **Use Alternate Assets**. The set used for development:

| Mod | Source | Notes |
|---|---|---|
| MM Reloaded 4K (2Ship O2R) | [evilgames.eu](https://evilgames.eu/texture-packs/mm-reloaded.htm) | Load first |
| 3DS Link (standard) and 3DS Equipment | [GameBanana 644553](https://gamebanana.com/mods/644553) | Load after the texture pack |
| HD Moon | [GameBanana 640438](https://gamebanana.com/mods/640438) | |

## Workflow

- `main` is the integration branch and always builds.
- Work happens on `feature/*` or `fix/*` branches and lands through pull requests.
- A change that touches the renderer is two pull requests: one into `new-dawn` on New-Dawn-libultraship, and one
  here that moves the submodule pointer. Merge the libultraship one first.
- Rendering changes need before/after screenshots in the pull request and an independent review before merging.
- `upstream` remotes track 2 Ship 2 Harkinian and libultraship for pulling in their fixes.
