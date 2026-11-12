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

### ND-005 — Explicit HDR world and display-colour boundary
- **Date:** 2026-11-12
- **Status:** Accepted experiment (lead-approved HDR design)
- **Decision:** Extend ND-003 with a `WORLD_BEGIN` colour phase and scene-end display conversion. RGBA16F replaces only world colour; the original depth/stencil attachment, canonical framebuffer IDs and gameplay depth queries remain intact. Legacy captures store display colour, including early tonemapped snapshots.
- **Tradeoffs / rollback:** Default off; single-sample only. Incompatible scene effects are gated off, and Deku bubble refraction falls back to LDR. Unsupported combiners retain their bounded artistic result. Disable `gNewDawn.HDR.Enabled` to restore the existing renderer.
- **Controls:** `gNewDawn.HDR.Enabled` (0), `.Exposure` (EV, default -0.2, -8..8), `.SunIntensity` (1.8, 0..16), `.AmbientFill` (0.4, 0..4), `.Debug.Inspect` (every 300 frames: float peak, pixels>1, depth samples; keep off for timings). Defaults tuned from side-by-sides at Termina Field and South Clock Town (08:00, 12:00); the earlier 4.0 sun blew out noon surfaces.
- **Mechanism:** `WORLD_BEGIN` (z_play.c, before the skybox) swaps the game FBO's colour for RGBA16F; `SCENE_END` (or any early copy/readback/`SelectTextureFb`, as a snapshot) applies `2^EV`, a Narkowicz ACES fit, sRGB encode and dither into the original RGB8 texture. Only the `TEXEL0 * SHADE` family (optionally times one prim/env tint) gets linear albedo times unquantised irradiance; everything else keeps its bounded artistic result, decoded to linear before fog. Sky and unlit draws are not yet calibrated.
- **Gated under HDR:** AO, sun shadows, shafts, water, bloom and grade (LDR-only, RGB8 targets); MSAA above 1 (whole feature falls back, logged once); frames with a Deku bubble actor (it samples a display-colour capture as a texture). Pause and pictograph buffers stay display colour; saved pause backgrounds skip HDR.
- **Verified 2026-11-12 (RX 7800 XT, 1080p, MSAA 1):** off is byte-identical to main in both test scenes; pre-tonemap peak 1.76-1.97 (up to 103k pixels over 1 at noon); depth samples and `GetPixelDepth` equal on/off; opaque HUD patches identical; display conversion 0.03-0.04 ms GPU (legacy AO plus bloom/grade: about 0.47-0.58 ms, gated off in HDR; total world cost unmeasured); 0 GL errors over 300-frame live-to-pause runs. Pictograph, Deku bubble, resize, transitions and repeated pause were not exercised.
- **Next:** linear AO/shadow application with direct-versus-ambient separation, HDR bloom, sky calibration, float MSAA, display-source materials, then HDR in the preset.
