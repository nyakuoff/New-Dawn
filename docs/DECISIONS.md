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
- **Decision:** World-space opaque triangles are recorded during the opaque pass. At the OPAQUE_END marker they are rendered to a sun-aligned orthographic shadow map centred on the player. A screen-space pass then reconstructs world position with the exact inverse projection and attenuates the estimated direct-light fraction (no separate direct-light attachment).
- **Alternatives considered:** a previous-frame shadow map (rejected: actor shadows lag a frame) and a second display-list walk (rejected: double CPU cost).

### ND-005 — Explicit HDR world and display-colour boundary
- **Date:** 2026-11-12
- **Status:** Accepted M1 experiment (continued by ND-006)
- **Decision:** Extend ND-003 with a `WORLD_BEGIN` colour phase and scene-end display conversion. RGBA16F replaces only world colour; the original depth/stencil attachment, canonical framebuffer IDs and gameplay depth queries remain intact. Legacy captures store display colour, including early tonemapped snapshots.
- **Tradeoffs / rollback:** Default off; single-sample only. Incompatible scene effects are gated off, and Deku bubble refraction falls back to LDR. Unsupported combiners retain their bounded artistic result. Disable `gNewDawn.HDR.Enabled` to restore the existing renderer.
- **Controls:** `gNewDawn.HDR.Enabled` (0), `.Exposure` (EV, default -0.2, -8..8), `.SunIntensity` (1.8, 0..16), `.AmbientFill` (0.4, 0..4), `.Debug.Inspect` (every 300 frames: float peak, pixels>1, depth samples; keep off for timings). Defaults tuned from side-by-sides at Termina Field and South Clock Town (08:00, 12:00); the earlier 4.0 sun blew out noon surfaces.
- **Mechanism:** `WORLD_BEGIN` (z_play.c, before the skybox) swaps the game FBO's colour for RGBA16F; `SCENE_END` (or any early copy/readback/`SelectTextureFb`, as a snapshot) applies `2^EV`, a Narkowicz ACES fit, sRGB encode and dither into the original RGB8 texture. Only the `TEXEL0 * SHADE` family (optionally times one prim/env tint) gets linear albedo times unquantised irradiance; everything else keeps its bounded artistic result, decoded to linear before fog. Sky and unlit draws are not yet calibrated.
- **Gated under HDR:** AO, sun shadows, shafts, water, bloom and grade (LDR-only, RGB8 targets); MSAA above 1 (whole feature falls back, logged once); frames with a Deku bubble actor (it samples a display-colour capture as a texture). Pause and pictograph buffers stay display colour; saved pause backgrounds skip HDR.
- **Verified 2026-11-12 (RX 7800 XT, 1080p, MSAA 1):** off is byte-identical to main in both test scenes; pre-tonemap peak 1.76-1.97 (up to 103k pixels over 1 at noon); depth samples and `GetPixelDepth` equal on/off; opaque HUD patches identical; display conversion 0.03-0.04 ms GPU (legacy AO plus bloom/grade: about 0.47-0.58 ms, gated off in HDR; total world cost unmeasured); 0 GL errors over 300-frame live-to-pause runs. Pictograph, Deku bubble, resize, transitions and repeated pause were not exercised.
- **Next:** linear AO/shadow application with direct-versus-ambient separation, HDR bloom, sky calibration, float MSAA, display-source materials, then HDR in the preset.

### ND-006 — HDR scene effects and multisample presentation
- **Date:** 2026-11-12
- **Status:** Accepted (HDR milestone 2)
- **Decision:** Keep scene colour in RGBA16F through AO, shadows, shafts, water/SSR and bloom. Resolve multisampled RGBA16F into same-format scratch before sampling. Add bloom radiance before the display transform; grade after the transform and sRGB encoding, then dither last. The New Dawn preset enables HDR and MSAA 4; individual features remain switchable.
- **Depth / capture contract:** Replace only the game FBO's colour attachment. Keep its original DEPTH24_STENCIL8 storage, sample count and GetPixelDepth implementation. Early copies/readbacks sample a private single-sample display snapshot. At scene end restore the original RGB8 colour attachment and shader-copy display colour into it, including when multisampled. Later HUD, pause and capture draws remain in display colour.
- **Atmosphere:** Explicit skybox begin/end markers classify sky draws without changing their order. HDR-only SkyIntensity and FogIntensity scale decoded artistic colours; fog ranges/factors and environment time remain unchanged.
- **Tradeoffs:** AO multiplies the linear opaque scene because no separate indirect-light attachment exists. Shadows estimate the direct/ambient ratio using HDR light scales; points, emission and baked texture shadows cannot be isolated exactly. A direct-light auxiliary attachment remains future work. The existing Deku-bubble LDR fallback and bounded unsupported combiners remain.
- **Rollback:** Disable gNewDawn.HDR.Enabled (also exposed in the graphics menu). Reset Graphics restores the previous opt-in HDR default. The existing LDR effect branches remain; downsample, shadow and composite shaders compile separate HDR/LDR variants from shared GLSL so the HDR arithmetic does not perturb legacy rounding. The bloom threshold retains its legacy clamp in LDR.
- **Validation:** See docs/HDR_M2.md for capture commands, image review, GPU measurements and unexercised cases. No proprietary assets or game captures are added to the repository.

### ND-007 — The menu is pages over the existing widget registry; the look is set once, on first run
- **Date:** 2026-11-14
- **Status:** Accepted
- **Decision:** The 2Ship settings menu is replaced by a New Dawn menu (`docs/MENU.md`): a rail of pages, each page a row of tabs, each tab one `(entry, sidebar)` widget list from the existing `AddWidget` registry. Other modules' init functions keep registering widgets unchanged; only presentation and organisation changed. The five looks (Ultra, High, Medium, Low, Original look) are one table of cvars in `NewDawnPresets.cpp`, shared by the menu and `capture.py --preset`.
- **Defaults:** The compiled cvar defaults are unchanged, so "Original look" is exactly the pre-existing N64 path and existing configs behave as before. A missing or setting-less config (`CVars` empty) is a fresh install: the Ultra look is written once and `gNewDawn.Setup.Version=1` records it. A config that already has settings gets a one-time popup (Apply / Keep) instead of a silent change. Asset mods (`gEnhancements.Mods.AlternateAssets`) are never part of a look.
- **Naming:** `gNewDawn.HDR.Enabled` is the linear lighting pipeline with SDR output, not HDR monitor output, so players see "Modern Lighting Pipeline". Real HDR output would be a separate, default-off setting.
- **Rollback:** Setting `gNewDawn.Setup.Version` to 0 and removing the other `gNewDawn.*` keys reruns first-run setup; any look can be switched from Graphics > Quality at any time.

### ND-008 — Every New Dawn asset ships with the game; game-derived data is generated on the player's machine
- **Date:** 2026-11-15
- **Status:** Accepted
- **Decision:** New Dawn must be packageable for players. Every asset it uses (effect textures, flares, grass, caustics, star data, sky panoramas, fonts, UI art) is redistributable (CC0, CC-BY or CC-BY-SA with attribution, public domain, or made by a script in `tools/newdawn/`) and committed with its licence and credits. If an asset can't be redistributed, it isn't used.
- **Game-derived data:** anything generated from the game's own data (the material maps and the texture recolour, both built from the game's textures) is never committed or shipped. It is generated on the player's machine from their own dump, like 2Ship's asset extraction.
- **Exception:** optional community HD mods (texture packs, 3DS models) stay separate downloads that the player installs themselves.

### ND-009 — Material maps and recolour are generated in-game, cached, and mounted above every mod
- **Date:** 2026-11-15
- **Status:** Accepted
- **Decision:** The normal/roughness/relief maps and the albedo recolour are built by a C++ port of `materials.py` and `recolor.py` (`mm/2s2h/NewDawn/RuntimeMaterials.cpp`) from the HD textures the player has loaded, on first launch (all cores, a few seconds, themed progress popup) and cached under `<app dir>/cache/newdawn-materials/live` with a signature (recipe version, recipe data, loaded archives' name/size/mtime). They are mounted explicitly after all mods, are not mods (not in `mods/`, `EnabledMods` or the Mod Menu) and do not depend on `gEnhancements.Mods.AlternateAssets`, which stays the player's choice for community packs. Recipes and classification are our own data under `mm/assets/custom/newdawn_materials/`, packed into `2ship.o2r`.
- **Why:** ND-008 forbids shipping them and players have no Python. The port reproduces the Python archives byte-for-byte (4,417 of 4,418 map entries; 42 of 42 recolours), so the look is unchanged.
- **Consequences:** The generator needs an HD pack with `alt/scenes/` textures; with none it does nothing. The Settings button regenerates in the background and applies on the next launch because the archive manager cannot be mutated while workers read it. The Python tools stay as the reference implementation (`tools/newdawn/compare_maps.py` checks the port). See `docs/RUNTIME_MATERIALS.md`.

### ND-010 — Temporal anti-aliasing jitters the projection in the interpreter and resolves before bloom
- **Date:** 2026-11-15
- **Status:** Accepted
- **Decision:** While the HDR world is open (`WORLD_BEGIN` to `SCENE_END`) the interpreter shifts every perspective projection the game loads by a 16-sample Halton sub-pixel offset (`P_matrix` itself, so all New Dawn passes that unproject from it stay consistent with the depth buffer). Orthographic loads and everything after `SCENE_END` (HUD, 2D, text, pause) are never shifted. The renderer reprojects last frame's resolved image with the un-jittered current and previous view-projections and the depth buffer (no per-object motion vectors), clips it to the 3×3 neighbourhood in tone-compressed YCoCg, rejects it by depth range, trusts it less on actors (the local-lights stencil mask), and resolves in scene-referred light before bloom and the display transform. Grass tufts are their own class (the grass pass writes stencil 2; the mask target is RG8): capped history, a narrower current-frame filter and full sharpen, because swaying blades cannot be reprojected and a soft filter blurs them. A small contrast-adaptive sharpen that fades out in the darks (perceptual luma 0.04-0.14) follows; history is stored unsharpened.
- **Why not alternatives:** per-vertex jitter in the vertex stage (after the aspect adjustment) would leave grass, VFX, AO and the other passes unjittered, exactly where the shimmer is; a velocity buffer would need every actor transform and every render-only animation (wind sway, tufts) to write it; a point-sampled current frame pulses once per jitter cycle on still edges, so the current samples are filtered by distance to the pixel centre.
- **Tradeoffs / rollback:** Needs the Modern Lighting Pipeline and OpenGL; default off outside Ultra/High; with `gNewDawn.TAA.Enabled=0` nothing is jittered or drawn and the frame is byte-identical to main (frozen Stock Pot Inn). Moving things that animate themselves (wind, water, effects, actors) are held by the clip, not accumulated. History resets on frame gaps, resizes and camera cuts. Upscaling is out of scope but the jitter, un-jittered matrices, depth copy, mask and history are what it needs. Details and measurements: `docs/TAA.md`.

### ND-011 — World-space GI: a render-side voxel copy of the rooms, lit and gathered by a probe grid
- **Date:** 2026-11-15
- **Status:** Accepted
- **Context / evidence:** Bounce light was screen-space only (`docs/SSGI.md`): nothing off-screen contributes, it changes as things enter and leave the view, one bounce, no occlusion by walls. The renderer has no scene, only the display lists of the current frame, and the game culls room meshes that are wholly behind the camera. Measured: outdoor rooms are lit geometry, interiors and dungeons are unlit and vertex-coloured; Termina Field and Great Bay Coast are 25,000+ units across.
- **Decision:** The interpreter records the opaque triangles of every room draw (a marker pair around `Room_Draw`; actors are never recorded; the entries the game culled arrive as a record-only list, the #102 mechanism, on recording frames) with a reflectance estimated from the combiner and the mean texture colour. The OpenGL backend voxelises them into a wrap-around window (the whole scene when it fits at 24 units per voxel, otherwise following the camera in 16-voxel steps); a voxel is written once and kept until the scene changes. A radiance volume lights the voxels (sun or moon with a voxel shadow ray, occluded sky, local lights, the painted colour of baked geometry as an emitter, and the probes' own irradiance for further bounces). A 32×16×32 window of irradiance + distance probes traces a budgeted block of probes per frame through the voxels and blends with hysteresis; a deferred pass applies it per pixel with Chebyshev visibility. `gNewDawn.GI.World` = 0 off, 1 High, 2 Ultra. Screen-space GI stays as near-field detail at half weight.
- **Alternatives considered:** recording only what the game draws and letting the grid fill in as the camera looks around (the first version; walls behind the camera were missing until seen); voxel cone tracing without probes (no leak control, noisier); tracing probes against the shadow-caster list or depth cubes (per-frame geometry only, nothing behind the camera); an albedo attachment in the world pass for the receiving surface (touches every shader variant and the MSAA targets).
- **Tradeoffs / rollback:** Needs OpenGL 4.4 (image load/store, `glClearTexSubImage`) and 118 MB of VRAM on Ultra (26 MB on High). Rooms that are not loaded are not in the grid; actors neither block nor bounce. The reflectance, the per-source gains and the daylight limit on dark receivers are art direction for this game's dark textures, not physics. Voxel reflectance is one colour per triangle. `gNewDawn.GI.World=0` emits no markers and runs none of it (frame byte-identical to main, frozen Stock Pot Inn). Details and measurements: `docs/WORLD_GI.md`.
- **Validation:** `docs/WORLD_GI.md` (eight scenes off / SSGI / world, rotation bursts, leak checks, GPU ms per tier, off identity, 0 GL errors).

### ND-012 — Characters are rounded by tessellated PN patches whose adjacency is rebuilt once per triangle command
- **Date:** 2026-11-15
- **Status:** Accepted
- **Context / evidence:** Character models are a few hundred triangles in rigid limb segments; their silhouettes are polygons and lighting shows every facet. The vertex stream reaching the renderer has no adjacency, limbs share vertices only through coinciding positions, and animations must not change.
- **Decision:** Lit triangles of skeleton actors are drawn as cubic PN patches by tessellation stages added to the ordinary combiner programs. The patch goes through the model's own points; per corner the interpreter supplies the offsets of the two neighbouring edge control points. Those come from a one-off analysis (position weld, normal clusters by crease angle, edge kinds normal / crease / straight) stored per triangle command in model space, so every later frame is a lookup. Both triangles on an edge are given identical offsets and the stages evaluate them symmetrically, so there are no cracks. Bulges are bounded by the narrowest adjacent triangle, suppressed on near-flat panels and reduced at points.
- **Alternatives considered:** CPU subdivision in the interpreter (multiplies the single-threaded vertex cost); Phong tessellation (no per-edge control, so hard edges cannot be kept); plain PN triangles from each triangle's own normals (cracks wherever twins differ, measured); analysing per actor draw or per display list (parts that appear later, shared display lists and instanced draws ended up with different neighbours, found by the crack check); subdivision surfaces (move the model's points, need clean closed meshes).
- **Tradeoffs / rollback:** OpenGL 4.0 only. Shadow maps keep the original triangles. Flat-shaded limbs (split normals) are treated as hard edges and stay faceted. A new triangle command is drawn flat for two frames. `gNewDawn.Characters.Smooth=0` restores the previous frame byte for byte.
- **Validation:** `docs/SMOOTH_CHARACTERS.md` (same-pose off/on crops, animation burst, silhouette overlap, crack check, GPU and CPU cost, off identity, 0 GL errors).

### ND-013 — Direction for modern models and textures: stylised current-gen, local-only, MM3D optional
- **Date:** 2026-11-15
- **Status:** Accepted (maintainer decision on the research in `docs/research/MODELS_TEXTURES_*.md`)
- **Visual target:** *stylised current-gen*: one coherent, clean material language across the whole game, in the spirit of modern stylised remakes. Recognisable, not photoreal.
- **IP posture: local-only.** New Dawn ships code, its own generic content (CC0, CC-BY or CC-BY-SA, or made by our scripts) and recipes. Nothing that depicts Nintendo's characters or places is shipped. Anything Nintendo-shaped (upscaled originals, converted 3DS models, derived maps) is generated on the player's machine from their own dumps. This extends ND-008.
- **MM3D dump: optional.** Players who supply their own *Majora's Mask 3D* dump may get models converted from it at first run. Players without one keep the N64 models with render-time smoothing (ND-012).
- **Consequences:**
  - The community texture pack stays an optional extra. New Dawn's material system must work without it (local restore of all world and character textures plus object-path material maps; research plan A).
  - Our own generic stylised material library (plan C) is allowed because it depicts no Nintendo designs.
  - Commissioned character art (plan D) is out.
- **Next:** spikes before committing to a pipeline: texture screen coverage plus local restore with detail injection, and an MM3D conversion of the Clock Town guard and Anju bound to the N64 limbs. A skinning prototype follows if the MM3D spike needs smooth weights.

### ND-014 — Art direction: the 2026 *Ocarina of Time* remake look, not cartoony (supersedes ND-013's visual target)
- **Date:** 2026-11-15
- **Status:** Accepted (maintainer)
- **Target:** the art direction of Nintendo's 2026 *Ocarina of Time* remake (Switch 2):
  - **Environments:** hyper-detailed and tactile, with "the analog feel of practical-effects films", like a richly crafted physical set or miniature, in a dark, beautiful world.
  - **Characters:** deliberately doll- or puppet-like, simpler and colourful, set against that world.
  - **Not cartoony, not smooth clay.** ND-013's "stylised current-gen" target, with Link's Awakening 2019 as the example, is withdrawn; a spike built on it read as cartoony.
- **Unchanged from ND-013:** local-only IP, an optional MM3D dump, and no native upgrades (assets are replaced, not restored).
- **Consequences:**
  - **Environment textures:** generic environment materials come from art-directed CC0 PBR libraries (ambientCG, Poly Haven): recoloured to each original's palette on the player's machine, scaled to the original UVs, with tactile wear, grime and moss.
  - **Character models:** they follow the MM3D conversion plus character shading with fabric, felt and wood-like tactile detail.
  - **Shelved:** the smooth procedural "clay" library from the first spike; its tiling, colour-matching and mapping tools are reused.

### ND-015 — 3DS character models: converted on the player's machine, flex-welded onto the N64 skeleton, behind an overlay prefix
- **Date:** 2026-11-16
- **Status:** Accepted (maintainer approved plan B after the spike, `spike/mm3d-spike`)
- **Decision:** NPC and enemy models come from the player's own *Majora's Mask 3D* RomFS (`gNewDawn.MM3D.RomFS`), converted at first launch by a C++ port of the spike converter (`mm/2s2h/NewDawn/Mm3dCharacters.cpp`) into `<app dir>/cache/newdawn-mm3d/live/mm3d-characters.o2r`. Nothing from the dump is committed or shipped (ND-008, ND-013). Only skeletons whose rooted limb hierarchy equals the 3DS CMB's are converted (58 Flex skeletons, table generated by `tools/newdawn/mm3d/chartable.py`, names only); a model is enabled only after it has been reviewed in game.
- **Binding:** every vertex goes into the local space of its dominant limb and is drawn under that limb's matrix through the N64's own Flex matrix array (segment 0xD), so shared vertices cannot separate. No renderer skinning (the spike's rigid binding left cracks; true weighted skinning stays optional).
- **Switch:** the cache is stored under the prefix `mm3d/` and enabled by `ResourceManager::SetOverlayEnabled` (libultraship), which is tried before `alt/`. The switch (`gNewDawn.MM3D.Characters`) does not depend on `gEnhancements.Mods.AlternateAssets`, and asset mods keep working. It is disabled until a cache is mounted.
- **Colour:** the 3DS material light constants are folded into the texture (`LightRef / (0.6 diffuse + 0.4 ambient)` and a saturation lift); defaults 0.6 and 1.35 follow the 2026-11-16 art direction (colourful, tactile characters in a darker world).
- **Alternatives considered:** extending the AlternateAssets switch (couples the models to asset mods); rigid per-limb binding (cracks, mean seam gap 3 to 5 % of height in the spike); interpreter skinning first (3 to 5 days, not needed for a crack-free result); shipping converted models (forbidden by ND-013).
- **Tradeoffs / rollback:** triangles that span a joint stretch (p99 about 2 to 3.4x). Faces are static until the CMAB animations are mapped. `gNewDawn.MM3D.Characters=0` (or no dump) leaves the frame pixel-identical to main (frozen Stock Pot Inn).
- **Validation:** `docs/MM3D_CHARACTERS.md` (counts, colour studies, off identity, validation set and its gaps).

### ND-016 — 3DS character textures are upscaled 4x on the player's machine with a CC0 CPU network; transfer and detail maps are not adopted yet
- **Date:** 2026-11-16
- **Status:** Accepted (maintainer approved AI upscaling of the 3DS textures)
- **Context / evidence:** the 3DS models are better than the N64 ones but their textures are 128 px against the pack's 1024 px. Compared 1:1 in game on the guard, Anju and the receptionist: bicubic, PBRify SPAN, Real-ESRGAN anime, HD transfer from the N64 model, and normal/roughness maps (`docs/MM3D_CHARACTERS.md`, "Textures").
- **Decision:** the converter runs PBRify SPAN V4 (CC0-1.0, weights committed under `mm/assets/newdawn/mm3d/`) over every character texture before the colour mapping, in a C++ port that matches ONNX Runtime within 1/255 (no ONNX Runtime dependency). `gNewDawn.MM3D.Upscale` switches it.
- **Alternatives considered:** Real-ESRGAN anime (sharper faces, destroys the guard's mail, dataset licence unstated); transfer from the HD pack (artefacts where designs differ, needs a reliability mask); detail maps (skin and faces degrade without a class mask and tone compensation); ONNX Runtime in the build (a large dependency for one 1.7 MB network).
- **Tradeoffs / rollback:** up to about 60 s of one core if all 58 models are enabled (cached afterwards); the textures stay softer than the 4K pack on faces. `gNewDawn.MM3D.Upscale=0` restores the 3DS size.
- **Validation:** side-by-side sheets in `~/dev/ghm-mods/showcase/mm3d-characters/textures/`, numeric match to ONNX Runtime, 0 GL errors.

