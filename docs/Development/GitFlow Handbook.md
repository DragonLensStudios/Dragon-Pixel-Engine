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

- Starts from `develop`.
- One cohesive feature or defect.
- Ends in a PR to `develop`.

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

The cross-platform workflow runs for pushes to `main`, `develop`, `feature/**`, `release/**`, and `hotfix/**`, and for pull requests targeting `main` or `develop`. Its policy job enforces these accepted relationships:

- `feature/*`, `release/*`, and `hotfix/*` may target `develop`.
- Only `release/*` and `hotfix/*` may target `main`.
- Manual dispatch is allowed only from a documented GitFlow branch.

Every configure, build, and test command must propagate its native exit code. Each platform/configuration must produce readable CTest JUnit evidence with at least the repository-defined minimum number of tests and zero failures or errors. A missing, malformed, empty, partial, failed, or errored report is not a passing check.

Keep candidate required-check job names unique and stable. Configure branch protection only after the named checks have run truthfully on the merged workflow; do not require a skipped, ambiguous, failing, or not-yet-observed check.

## Pull request cadence

Open a draft PR when the branch has enough structure to review, not only at the final minute.

A few focused commits may form one coherent PR.
Do not create one PR per trivial commit.
Do not bundle multiple unrelated features into one PR.

## Merge policy

Preferred default: squash or merge according to repository policy, but preserve meaningful commit history for complex feature work when the commit sequence documents reasoning and verification.

Never merge without review.
Never force-push shared branches without explicit approval.
