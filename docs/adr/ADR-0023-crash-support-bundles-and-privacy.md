# ADR-0023: Crash Reports, Support Bundles, and Privacy

> **Status:** Proposed
> **Design revision:** `DPE-ARCH-0009`
> **Last reviewed:** 2026-07-25

## Context

Editor, worker, importer, migration, build, package, plugin, and updater failures need enough correlated evidence to reproduce defects and prove recovery. Crash dumps, logs, audit events, paths, prompts, project source, assets, environment data, and session credentials can also expose highly sensitive information. Supportability cannot create a default telemetry channel or silently transfer user data.

## Decision

- `SupportBundleService` is the sole editor service that assembles and exports support bundles. Each supervised process may emit a bounded structured crash envelope to a per-user local diagnostic store; it may not upload data or append arbitrary project files.
- Crash collection and support bundles are local and user-initiated by default. Dragon Pixel Engine enables no network telemetry, crash upload, analytics endpoint, or background submission by default. Any future network collection requires a separate reviewed architecture decision, explicit destination and category disclosure, and granular opt-in.
- A support-bundle manifest version 1 records engine/build and platform identity, selected files, redactions, content hashes, consent scope, correlation IDs, and retention intent. The exported archive is hash-bound to that previewed manifest.
- The export flow is select scope, copy into isolated staging, scan and redact, generate hashes/manifest, preview the exact inventory and redactions, allow removal of items, obtain explicit consent, and export to a user-selected destination. Cancelled, failed, or completed staging copies are deleted; orphaned staging is removed on the next startup after recovery validation.
- Default bundle content is limited to structured engine diagnostics, bounded recent logs, operation/crash correlation, public build/module/plugin manifest identities, recovery disposition, and non-identifying platform facts. Raw memory dumps, project documents, source, assets, external linked files, private prompt content, environment values, and personally identifying paths are excluded unless the user deliberately selects an eligible item after preview.
- Active secrets and session capability tokens are never eligible for export. Token/credential patterns and protected fields are removed before preview, and export fails closed if the required scan or redaction cannot complete. User-selected project material remains subject to the same secret scan and is copied; the original is never modified.
- Local diagnostic retention is finite and user controllable. Release configuration defines tested age, count, and byte limits; the oldest eligible records are removed when any limit is reached. The UI exposes retention settings and `Delete All`. Bundle staging is transient, and an exported archive becomes user-owned at its selected location; the manifest states that the application can no longer enforce retention after export.

## Consequences and tradeoffs

- Local opt-in collection gives users control and avoids a hidden telemetry channel, but maintainers receive less automatic crash data and users must choose to export and transmit a bundle themselves.
- Preview, redaction, hashing, and secret scanning add failure modes and latency. They are necessary because automatic path replacement or pattern matching alone cannot guarantee that arbitrary logs or dumps are safe.
- Excluding raw dumps and project content by default can reduce diagnostic depth. Explicit per-item inclusion supports difficult cases without treating sensitive content as routine telemetry.
- Finite local retention limits exposure and disk growth, but older crash evidence may expire before a report is filed.

## Security and ownership

- Users own project content, external linked sources, and exported bundles. Dragon Pixel Engine owns only its bounded per-user diagnostic store and transient bundle staging; neither ownership grants permission to upload or share data.
- `SupportBundleService` receives read access only to approved diagnostic sources and write access only to staging and the user-selected export target. Plugins, importers, automation clients, and project code cannot bypass the manifest, preview, redaction, consent, or retention path.
- Redaction operates on staged copies. It never alters authoritative logs, projects, assets, prompts, or external sources. Manifest hashes are computed after redaction so reviewers and recipients can verify the bytes actually exported.
- Crash envelopes and audit events use correlation IDs instead of embedding session tokens, full command payloads, prompts, or unnecessary absolute paths. Capability and integrity failures are themselves structured diagnostics without exposing the rejected credential.

## Validation and evidence gate

**POC S: Version 1.0 Release Qualification** must pass on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64. It must force editor and supervised-process failures; correlate recovery without project mutation; exercise support-bundle selection, preview, removal, redaction, hashing, consent, cancellation, export, orphan cleanup, retention limits, and `Delete All`; prove secret/session-token and default project/prompt/path exclusions with adversarial fixtures; and verify that a default installation makes no telemetry or crash-upload network request.

POC S must include security and privacy review and repeated crash/recovery evidence alongside the long-running, sanitizer, leak, handle, process, corruption, compatibility, accessibility, and documentation-reproduction gates. It is unimplemented as of 2026-07-25, so this ADR remains `Proposed`.
