# Release Workflow

## Developer Preview

A developer preview may expose incomplete systems, but must include:

- Exact supported platforms.
- Exact adapter status.
- Known limitations.
- Reproducible build instructions.
- Packaged integrity manifest.
- License and notices.
- A reference sample.
- Release notes.
- No production-ready claim.

## Release branch

Create from `develop`:

`release/<version>`

Allowed changes:

- Version source.
- Changelog.
- Packaging.
- Documentation.
- Release blockers.
- Test and install corrections.

Not allowed:

- New broad features.
- Architecture expansion unrelated to release.
- Silent threshold reductions.

## Finalization

1. Run complete required matrices.
2. Verify clean installation or portable launch.
3. Verify update/rollback when applicable.
4. Merge to `main`.
5. Tag release.
6. Merge release branch back to `develop`.
7. Publish artifacts and evidence.
