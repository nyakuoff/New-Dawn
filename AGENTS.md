# New Dawn — shared project rules

## Purpose
New Dawn is a **visual-only modernization** of *Majora's Mask*, built on a fork of **2 Ship 2 Harkinian**. Aim for a cohesive, polished visual overhaul faithful to the game's surreal, eerie art direction. The user wants agents to determine technical and artistic approaches independently, then deliver working, demonstrated changes.

## Hard boundaries
- Preserve original gameplay, player movement, combat, physics, enemy AI, NPC schedules, quests, puzzles, progression, story, dialogue, cutscene sequencing, and **existing animations**.
- Geometry, model, material, shader, lighting, post-processing, effects, UI, and asset-pipeline changes are within scope **only when mechanical behavior stays unchanged**.
- Do not silently change collision, hitboxes, trigger zones, scripting, game timing, save compatibility, or animation rigs. New visual assets must remain compatible with existing animation data.
- Do not make an architecture rewrite merely because it is possible; evaluate incremental changes first.
- Never claim a visual improvement was verified solely because code compiled.
- Do not download, commit, or redistribute proprietary game assets, ROMs, decryption keys, or licensed third-party art. Use an authorized local game dump provided by the developer; respect licenses of all imported dependencies and assets.

## Working practices
- Verify the **actual fork and build commands** before documenting them. Do not assume upstream paths or scripts match this fork.
- Prefer a minimal, reviewable change with an observable before/after result. Use conventional source style and existing build/test tooling.
- Capture a baseline before significant visual changes. Test rendering in-game, including representative camera angles and time-of-day states when applicable.
- Preserve a working branch. Use branches/worktrees for risky or parallel tasks. Avoid changing another worker's uncommitted files.
- Do not overwrite or revert user changes. Ask before destructive operations, publication, external account actions, or irreversible asset conversions.
- For performance changes, compare the same scene, settings, and hardware. Record what was measured and what remains unknown.
- Limit context: do not stream huge command output into the chat; save logs and show focused errors or summaries.

## Read only when relevant
- `docs/STATE.md` — current milestone, work in progress, and next step (start here).
- `docs/ARCHITECTURE.md` — verified map of the fork and visual extension points.
- `docs/VALIDATION.md` — acceptance criteria, visual comparisons, and regression checks.
- `docs/DECISIONS.md` — durable design decisions and their rationale.
- `docs/NEW_DAWN.md` — project explanation.

## Definition of done
An implementation is complete only when its changed files are clear, relevant checks have run (or blockers are recorded), original-game behavior is preserved as far as verified, and its visual impact is shown or explicitly marked as unverified.
