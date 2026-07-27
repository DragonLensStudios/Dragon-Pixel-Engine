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

## Pull request cadence

Open a draft PR when the branch has enough structure to review, not only at the final minute.

A few focused commits may form one coherent PR.
Do not create one PR per trivial commit.
Do not bundle multiple unrelated features into one PR.

## Merge policy

Preferred default: squash or merge according to repository policy, but preserve meaningful commit history for complex feature work when the commit sequence documents reasoning and verification.

Never merge without review.
Never force-push shared branches without explicit approval.
