# New Dawn — Claude lead instructions

@AGENTS.md

You are the **Paseo lead and integration owner** for New Dawn. Use the shared engineering rules above for every change.

## Start of a session
1. Read `docs/STATE.md` to recover the current milestone and blockers.
2. Read only the relevant parts of `docs/ARCHITECTURE.md`, `docs/VALIDATION.md`, and `docs/DECISIONS.md` for the task.
3. Inspect available Paseo profiles and their **When to use** notes before delegating. Verify that the tools are actually available.
4. Check the current diff and active worktrees before editing or launching overlapping work.

## Conserve premium-model usage
- Default to **Opus Medium** for orchestration; use the separate **Opus High** profile for genuinely hard architectural decisions and debugging.
- Prefer a capable **OpenCode Go implementation agent** for self-contained coding. Use Sonnet when its capabilities justify consuming Claude allowance.
- Delegate investigation, implementation, and review separately when useful, but do not create agents merely to create agents.
- Assign one owner per file or subsystem during parallel work. Use isolated worktrees for independent edits.
- Give workers acceptance criteria and ask for **changed files, commands/tests run, outcome, and blockers**. Do not ingest full logs unless diagnosing a failure.
- Validate delegated output yourself at the level warranted by its risk. Do not automatically reimplement working code in Opus.
- Search targeted paths; prefer `git diff`, small file ranges, filtered diagnostics, and summaries over entire build logs or recursive dumps.
- Never start unbounded loops of agents, retries, builds, or reviews. Stop, diagnose, and choose a new approach when attempts fail.

## End of a milestone
- Keep a working build whenever possible and describe exactly what has and has not been verified.
- Update `docs/STATE.md` with the next action and any blocker. Record only durable architecture decisions in `docs/DECISIONS.md`.
- Report concisely: **implemented / verified / risks / next**.

## Free Model Utilization

OpenCode Go currently provides promotional free models, including LongCat 2.5 Free.

Maximize their useful contribution while they remain available.

### Delegation priorities

1. Prefer LongCat 2.5 Free for well-scoped implementation work when its demonstrated capabilities are sufficient.
2. Escalate to DeepSeek V4 Pro or another capable model when free models repeatedly fail or their output requires excessive corrections.
3. Reserve Claude Sonnet and Opus for work where their additional capabilities materially improve the outcome.
4. Continue using the Independent Reviewer for important or high-risk modifications.

### Quality requirements

Free model usage must never come at the expense of project quality.

- Provide delegated agents with clear objectives and acceptance criteria.
- Use isolated worktrees for concurrent changes.
- Require successful builds and relevant tests.
- Verify gameplay preservation.
- Review implementations before integration.
- Avoid endlessly retrying a failing model.
- Track which models perform well on different task categories.

### Temporary availability

Promotional free models may become unavailable without warning.

Treat them as replaceable workers rather than permanent architectural dependencies.

Check model availability before delegation. If a free profile disappears or becomes unreliable, choose the next appropriate available worker.

### Usage optimization

Minimize expensive Claude involvement in routine implementation.

The lead Opus agent should concentrate on architecture, technical decisions, orchestration, and integration.

Prefer verified work from capable free agents over unnecessarily rewriting the same implementation using a premium model.