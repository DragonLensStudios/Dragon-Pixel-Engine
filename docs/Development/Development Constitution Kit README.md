# Dragon Pixel Engine Development Constitution Kit

This package provides the operating documents and Codex prompts for feature-by-feature GitFlow development.

## Install into the repository

Suggested paths:

- `AGENTS.md` at repository root
- Constitution and handbooks under `docs/Development/`
- Master prompt and start prompt under `prompts/`
- PR template under `.github/PULL_REQUEST_TEMPLATE/feature.md`
- Feature request template under `.github/ISSUE_TEMPLATE/feature.yml`

Review the proposed `AGENTS.md` against the repository's current version before replacing it. Preserve current project-specific architecture and documentation requirements that are more specific than this kit.

## Recommended first run

Give Codex:

1. `prompts/Dragon Pixel Engine Codex Master Prompt.md`
2. `prompts/Start Atomic Publication Recovery.md`

Codex should create one feature branch, complete and verify that feature, push it, and open a draft PR into `develop`. It must stop before merging.
