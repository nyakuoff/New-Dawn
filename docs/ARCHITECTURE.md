# New Dawn — verified architecture map

This document is an **evidence-backed map**, not a design wish list. Populate it after inspecting the actual fork. For each claim, cite file path / symbol / tested behavior or upstream documentation when appropriate.

## Repository and build
| Question | Verified finding | Evidence |
| --- | --- | --- |
| Fork revision / branch | Pending inspection | — |
| Linux build command and dependencies | Pending inspection | — |
| Run command and expected asset location | Pending inspection | — |
| Existing tests / formatting / CI | Pending inspection | — |

## Graphics pipeline
| Area | Entry points / owner files | Current capability | Constraint / experiment |
| --- | --- | --- | --- |
| Renderer / graphics backends | To inspect | Upstream documents OpenGL on Linux | Verify this fork |
| Shader compilation / management | To inspect | Unknown | — |
| Scene geometry and draw calls | To inspect | Unknown | — |
| Texture handling | To inspect | Unknown | — |
| Model and skeleton loading | To inspect | Unknown | Preserve animation behavior |
| Lighting and fog | To inspect | Unknown | — |
| Post-processing / frame buffer | To inspect | Unknown | — |
| UI and fonts | To inspect | Unknown | — |
| Alternate assets / mod packs | To inspect | Upstream documents `.o2r` / `.otr` | Verify tools and boundaries |

## Architecture principles
- Keep visual rendering and asset handling separate from gameplay behavior whenever practical.
- Prefer incremental, reversible improvements. Define the smallest interface needed before replacing large subsystems.
- Never make assumptions about renderer APIs or import formats based on other PC ports.
- Preserve compatibility with original skeletons and animation data, collision, timing, scripting and saved data.
- Capture evidence of visual output, not just successful compilation.

## Candidate experiments (not commitments)
Track potential improvements only after measuring feasibility and benefit. Each accepted experiment should include: scope, dependency, before screenshot/metric, success criteria, rollback path, and owner.

## Confirmed constraints and extension points
No repo-specific facts verified yet. Add findings as they are demonstrated.
