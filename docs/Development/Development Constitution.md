# Dragon Pixel Engine Development Constitution

## Purpose

This constitution defines how Dragon Pixel Engine is designed, implemented, tested, documented, reviewed, released, and sustained as an open-source project.

It applies to maintainers, contributors, contractors, automation, and AI agents.

## Foundational Principles

### User data is sacred

The editor must never trade authoring-data integrity for convenience. Save, migration, import, prefab, project creation, asset changes, and update operations must be transactional, validated, recoverable, and testable.

### Evidence outranks confidence

A claim is supported by a named test, build, platform run, artifact, or manual acceptance record. Confidence, local success, or architectural intent does not replace evidence.

### Architecture must remain legible

Subsystem ownership, dependency direction, process boundaries, contracts, and failure behavior must be explainable. Convenience layers must not conceal ownership or lifecycle.

### Open source is a product requirement

A contributor must be able to understand the project, build it, run tests, identify bounded work, submit a reviewable pull request, and receive accurate status information.

### Commercial sustainability complements openness

Revenue should come from expertise, support, integrations, training, templates, hosted convenience, and sponsorship. Core functionality should not be artificially crippled merely to create a paid tier.

## Development Model

Dragon Pixel Engine uses GitFlow.

- `main` contains releases.
- `develop` contains reviewed integration work.
- `feature/*` contains one cohesive feature.
- `release/*` prepares releases.
- `hotfix/*` repairs released defects.

No feature is considered finished until it has a reviewed pull request into `develop`.

Multiple independent feature branches may be active and reviewed concurrently. Each owns one cohesive scope, starts from the then-current `develop`, records an owner and affected boundaries, and normally targets `develop`. Parallel work does not waive aggregate review after another PR merges.

A genuinely dependent feature may be stacked on one unmerged `feature/*` branch only when both plans and PRs declare the dependency, overlap, ownership, merge order, retarget procedure, and required post-parent verification. The child cannot merge first. After the parent merges, the child is updated and retargeted to current `develop`, its aggregate diff is reviewed, and affected evidence is rerun. Shared history is not force-pushed without explicit contributor coordination.

## Feature Lifecycle

Each feature moves through:

1. Intake.
2. Architecture review.
3. Planning.
4. Branch creation.
5. Incremental implementation.
6. Focused verification.
7. Documentation.
8. Aggregate branch review.
9. Draft pull request.
10. Human review.
11. Merge into `develop`.
12. Post-merge validation when required.

## Review Philosophy

Review is not a ceremonial final click.

Review must examine:

- Whether the solution belongs in the selected subsystem.
- Whether an existing contract was bypassed.
- Whether state ownership is clear.
- Whether errors and recovery are credible.
- Whether tests prove the user-visible and failure behavior.
- Whether documentation matches implementation.
- Whether platform and support claims remain honest.
- Whether the diff contains unrelated changes.
- Whether the repository is easier to maintain afterward.

## Release Philosophy

Release versions represent verified capability, not accumulated code volume.

A developer preview may be incomplete but must be explicit about limitations.
A stable release must satisfy its declared support matrix and recovery obligations.
KNI remains experimental until its complete evidence gate passes.

## Governance

Technical authority is exercised through:

- Current accepted design documents.
- Architecture Decision Records.
- Versioned contracts.
- Tests and evidence.
- Reviewed pull requests.
- Release records.

No AI agent, contributor, or maintainer may silently override an accepted architecture boundary.

When architecture and implementation conflict, stop, document the conflict, and resolve it explicitly.
