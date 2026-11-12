# New Dawn — durable technical decisions

Record only decisions that materially constrain future work. Avoid logs, speculation, and repeated brainstorming. Update an existing record when its status changes; keep rejected alternatives visible briefly.

## Decision template

### ND-XXX — Short descriptive title
- **Date:** YYYY-MM-DD
- **Status:** Proposed / Accepted / Replaced / Rejected
- **Context / evidence:** What was measured or inspected? Cite paths, symbols, screenshots, or benchmarks.
- **Decision:** What we are doing, in one or two sentences.
- **Alternatives considered:** Why not the credible alternatives?
- **Tradeoffs / rollback:** Risks, dependencies, and how to reverse it.
- **Validation:** How success is tested.

---

## Recorded decisions

### ND-001 — Visual-only modernization scope
- **Date:** 2026-11-12
- **Status:** Accepted (user requirement)
- **Context / evidence:** The project's stated objective is visual fidelity without changing gameplay or existing animations.
- **Decision:** Graphics and assets may change; gameplay, AI, animation behavior, progression, collision, and timing must be preserved.
- **Alternatives considered:** Full gameplay remaster (out of scope).
- **Tradeoffs / rollback:** Some geometry changes will be constrained by original animation and gameplay behavior. Roll back visual changes that introduce mechanical regressions.
- **Validation:** Use `docs/VALIDATION.md` and targeted gameplay regression checks.

### ND-002 — Textures and models come from community mods
- **Date:** 2026-11-12
- **Status:** Accepted (user direction)
- **Decision:** Textures and models come from existing community mods (MM Reloaded 4K, 3DS Link and Equipment, HD Moon), installed locally through `mods/` and never committed. Engineering effort goes into rendering: lighting, post-processing, filtering, shadows.
- **Validation:** #6 (VRAM, stutter, conflicts).

### ND-003 — Rendering features run in libultraship's OpenGL backend behind display-list markers
- **Date:** 2026-11-12
- **Status:** Accepted
- **Decision:** New effects are driven by `G_NEWDAWN_POSTFX` markers that the game emits from `Play_DrawMain`, and are implemented in the OpenGL backend. Every effect has a `gNewDawn.*` cvar, and switching it off restores the original code path. The game's depth buffer and readbacks are never altered.
- **Note:** the game loads its shaders from `mm/assets/custom/shaders/`. That copy must be kept content-identical to libultraship's.

### ND-004 — Deferred, same-frame sun shadows
- **Date:** 2026-11-12
- **Status:** Accepted (in progress)
- **Decision:** World-space opaque triangles are recorded during the opaque pass. At the OPAQUE_END marker they are rendered to a sun-aligned orthographic shadow map centred on the player. A screen-space pass then reconstructs world position with the exact inverse projection and darkens only the direct-light term.
- **Alternatives considered:** a previous-frame shadow map (rejected: actor shadows lag a frame) and a second display-list walk (rejected: double CPU cost).
