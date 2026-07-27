# Codex Start Prompt: Atomic Publication Recovery

Use the Dragon Pixel Engine Codex Master Prompt and obey `AGENTS.md`.

Start from the latest `develop`.

Create:

`feature/atomic-publication-recovery`

Do not work on any other feature.

Your objective is to reproduce, diagnose, and repair the intermittent atomic-publication recovery defect that blocks a new authoritative complete matrix claim.

Required outcome:

1. A current feature plan.
2. A deterministic reproduction or the strongest bounded reproduction possible.
3. A root-cause analysis grounded in code and test evidence.
4. A repair that preserves transactional publication and the last known valid destination.
5. Regression coverage across interruption boundaries.
6. Focused Release verification.
7. Focused MSVC AddressSanitizer verification when native code changes.
8. Complete Windows Release matrix.
9. Complete Windows MSVC AddressSanitizer matrix.
10. Exact evidence and remaining blockers recorded.
11. Small focused commits.
12. Branch pushed.
13. Draft pull request opened into `develop`.
14. Stop for review without merging.

Before source changes, inspect the current branch, `git status`, related plans, related tests, and publication/recovery implementation. Preserve unrelated work.
