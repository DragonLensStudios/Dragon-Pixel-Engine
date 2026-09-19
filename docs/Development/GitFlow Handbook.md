# GitFlow Handbook

## Branches

### `main`

- Public releases only.
- Every release merge is tagged.
- No direct feature commits.

### `develop`

- Integration branch.
- Must remain launchable.
- Receives reviewed feature and release merges.

### `feature/*`

- Starts from `develop` by default.
- One cohesive feature or defect.
- Ends in a reviewed PR to `develop`.
- May temporarily start from and target one parent `feature/*` only for an explicitly documented dependency.

### `release/*`

- Starts from `develop`.
- No new product features.
- Versioning, packaging, documentation, and release blockers only.
- Merges to both `main` and `develop`.

### `hotfix/*`

- Starts from `main`.
- Repairs a released defect.
- Merges to both `main` and `develop`.

## Automated CI policy

The cross-platform workflow runs for pushes to `main`, `develop`, `feature/**`, `release/**`, and `hotfix/**`, and for pull requests targeting `main`, `develop`, or `feature/**`. Its policy job enforces these accepted relationships:

- `feature/*`, `release/*`, and `hotfix/*` may target `develop`.
- One non-identical `feature/*` branch may target one `feature/*` parent while an explicit dependency is recorded in both plans and PR bodies.
- Only `release/*` and `hotfix/*` may target `main`.
- Manual dispatch is allowed only from a documented GitFlow branch.

Every configure, build, and test command must propagate its native exit code. Each platform/configuration must produce readable CTest JUnit evidence with at least the repository-defined minimum number of tests and zero failures or errors. A missing, malformed, empty, partial, failed, or errored report is not a passing check.

Keep candidate required-check job names unique and stable. Configure branch protection only after the named checks have run truthfully on the merged workflow; do not require a skipped, ambiguous, failing, or not-yet-observed check.

## Pull request cadence

Open a draft PR when the branch has enough structure to review, not only at the final minute.

A few focused commits may form one coherent PR.
Do not create one PR per trivial commit.
Do not bundle multiple unrelated features into one PR.

### Concurrent independent work

- Contributors may keep multiple independent PRs open against `develop`.
- Each branch plan names its owner, scope, likely files/owners, and current base commit.
- Before implementation and before final review, fetch current `develop` and inspect overlapping merged work.
- Reviewed PRs merge one at a time. Every remaining branch then updates from current `develop`, resolves its own conflicts, reviews the aggregate diff, and reruns affected checks before merge.
- Avoid assigning two contributors the same authoritative owner or high-conflict file set unless the overlap and coordination are explicit.

### Explicit dependent stacks

Use a stack only when a child cannot be reviewed meaningfully without an unmerged parent. In both feature plans and both PR bodies record:

- parent branch, PR, and base commit;
- child owner and reviewer;
- dependency reason and overlapping files/owners;
- temporary PR target and required merge order;
- update/retarget procedure after the parent merges;
- checks that must rerun on the child against current `develop`.

The child PR targets the parent feature branch so its initial diff contains only child work. It remains draft and cannot merge before the parent. After the parent merges, the child author updates the branch from current `develop`, retargets the PR to `develop`, confirms the new aggregate diff, resolves conflicts without weakening gates, and reruns affected verification. A feature-to-feature PR relationship is review topology only; it does not authorize unrelated scope, automatic merging, or bypass of the final `develop` review.

## Merge policy

Preferred default: squash or merge according to repository policy, but preserve meaningful commit history for complex feature work when the commit sequence documents reasoning and verification.

Never merge without review.
Never force-push shared branches without explicit approval.
