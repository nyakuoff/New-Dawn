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
