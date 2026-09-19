# Dragon Pixel Engine 0.1.0 Documentation Baseline Plan

> **Status:** Documentation implementation verified; publication in progress; dependent draft required
> **Branch:** `feature/documentation-baseline-0.1.0`
> **Temporary target:** `feature/project-view-workflow`; final target `develop`
> **Owner:** Codex / Dragon Lens Studios; human maintainer review required
> **Started / updated:** 2026-09-18
> **Design revision:** `DPE-ARCH-0017` (unchanged)
> **Parent:** PR #7, `feature/project-view-workflow`, inspected at `fab41e28288681f37966345413d85f4eee405b79`
> **Exact child branch point:** `8cbdf421f6baca785e9e730dd54e4a521857f77a` (parent documentation-stack record)
> **Integration base:** `origin/develop` at `fa59b227e3057af603c569f1913b652d22b50c5a`

## Goal and user value

Make README the detailed public entry point for Dragon Pixel Engine, consolidate the complete existing prompt/result history under documentation version `0.1.0`, preserve the original prompt, explain licensing alongside MonoGame/KNI, and commit, push, and annotate a Git tag for the requested baseline. Capture the user's new prompt verbatim and distinguish the delivered result from historical evidence and future goals.

## Background and review topology

The user explicitly requested the shared Documents copy of the LLM Prompt Source, a versioned history, a comprehensive adaptive README, compatible licensing, and publication to GitHub. Both external roots contain the same current documentation. Initial Notes divergence was only two trailing blank lines in the external copies; normalization preserved every content line and restored all 66 mirrors. No unrelated worktree changes existed.

This child depends on the unmerged DPE-ARCH-0017 documentation and implementation in PR #7. Starting its content from older `develop` would omit accepted provenance and misdescribe the inspected Project View. Record the stack in the parent plan and PR before branching. Overlap: README, Prompt Source, and the parent/master plans; this child owns only documentation, licensing explanation, and baseline publication. Parent owner remains Codex / Dragon Lens Studios. The parent implementation and its Ubuntu blockers are unchanged.

Merge order: PR #7 first, this child second. After the parent merges, fetch current `develop`, update this branch without rewriting shared history, retarget the child to `develop`, review the aggregate diff, and rerun mirrors, original-prompt/history preservation, Markdown links, and whitespace checks. Reconcile new feature evidence before accepting the README. Keep the child draft until this is done. Neither PR is authorized for automatic merge.

## Scope and non-goals

- Preserve every historical request/result, source manifest, and architecture revision while presenting one `0.1.0` baseline with original prompt followed by result and supporting information.
- Keep the immutable first-structure file and original prompt transcription byte-identical.
- Document mission, current features, architecture/owners, workflows, build/run paths, repository map, roadmap, limitations, platform/adapter evidence, contribution workflow, and licensing.
- Verify upstream license facts and retain dependency notices. Record the license decision explicitly.
- Maintain physical UTF-8/no-BOM/LF copies in repository docs, the governed external root, and the user-selected shared root.
- No engine implementation, binary version, schema, ABI, protocol, security boundary, process topology, support promise, acceptance threshold, dependency upgrade, package release, or milestone closure changes. The `0.1.0` documentation label does not replace `DPE-ARCH-0017`.

## Existing architecture, ownership, and safety

Design remains authoritative for architecture; Prompt Source owns provenance; plans own exact evidence; README summarizes those records and inspected source. Authoritative project mutation remains service/command-owned. Preserve all user content and Git history. A tag identifies the documented snapshot and cannot imply passing product-release gates. No force-push, merge, or signed binary distribution is included.

## Implementation increments

1. Verify mirrors/revisions, read governance and active plans, inspect Git/PR state and documentation/source, record stack, and create the focused branch.
2. Reorganize provenance into `0.1.0`, preserve original/history bytes, and add this request/result and dated status clarification.
3. Rewrite README from actual sources with explicit implemented/planned distinctions and verified license references; keep applicable licensing records consistent.
4. Verify all mirrors, preserved content, links/anchors, source paths, scope, and `git diff --check`; review aggregate changes; commit/push; create a dependent draft PR and annotated tag; record final handoff.

## Verification and platform strategy

Documentation-only changes require exact mirror/UTF-8/LF validation, original prompt and retained-history preservation, local link/anchor checks, inspection of documented commands against scripts/presets, and aggregate diff review. Native/managed/Qt/runtime/sanitizer matrices are not rerun for prose-only changes and are not claimed as fresh results. Historical Windows, Ubuntu, macOS, MonoGame, and KNI results remain separately attributed to their plans and dates. Required hosted checks remain visible and are never bypassed.

## Risks and mitigations

- Historical claims masquerade as current: put a dated reading guide/current summary before retained history, with branch and feature evidence distinctions.
- Version consolidation destroys provenance: keep original prompt and all existing sections verbatim; verify mechanically before commit.
- License text implies dependencies are relicensed: identify project and dependency licenses separately, use primary upstream sources, retain Qt review gate.
- Tag suggests a qualified release: annotate exact documentation/source-snapshot scope and open gates; do not publish release binaries.
- Shared-root divergence: validate full file inventories and hashes in both external roots before and after editing.

## Definition of done

- [x] Initial 66-pair mirrors and DPE-ARCH-0017 fields verified in both external roots.
- [x] Parent stack record and child branch created.
- [x] Versioned prompt/result and detailed README complete.
- [x] License decision and primary-source verification recorded.
- [x] Preservation, links, formatting, mirrors, and aggregate review pass.
- [ ] Focused commits pushed, annotated tag verified remotely, dependent draft PR open.
- [ ] Durable final evidence and blockers recorded.
- [ ] Human review, parent merge, retarget/reverification, child merge and post-merge validation (outside this handoff).

## Work log

| Date | State | Evidence / decision |
| --- | --- | --- |
| 2026-09-18 | Intake | Fetched origin; clean worktree on PR #7 branch at `fab41e2`; `develop` remains `fa59b22`; no existing local tags. Both external full mirror checks pass 66 UTF-8/LF Markdown pairs at DPE-ARCH-0017 after Notes trailing-blank normalization. |
| 2026-09-18 | Research | Read Design and Prompt Source, governance, current plans, relevant ADRs, README/license, repository source layout, presets, and scripts. Upstream MonoGame and KNI LICENSE.txt identify Ms-PL plus inherited MIT portions. Tag and project-license preferences requested while independent documentation work continues. |

## Handoff

### 2026-09-18 implementation update

- Parent commit `8cbdf42` records dependency/ownership/overlap/retarget checks; its plan and PR #7 body were updated before creating the child. Parent branch was pushed without source changes.
- Owner explicitly selected **Keep MIT and document dependency compatibility**. `LICENSE.md` remains byte-identical. Added a mirrored licensing record with primary MonoGame/KNI/MIT/Ms-PL/Qt sources and the exact-package/distribution-audit limitation.
- Reorganized Prompt Source with original prompt first, consolidated result second, and all historical result/request/research/revision sections retained. Original Prompt section SHA-256: `6399cba759c35cd525258cdb0ffe9b3b7ad659c0dfdd8fe4fe192277946f44fc`. No original first-structure edit or Git-history rewrite.
- Expanded README with mission/goals, feature/status matrix, architecture diagram and owners, contract versions, project/component/input/prefab workflows, checked build pins, platform-separated evidence, roadmap, repository/document map, contribution upkeep, and license compatibility. Existing detailed Project/Tilemap/build workflows remain and obsolete Tilemap draft claims were corrected.
- Documentation-only scope: no native/managed/Qt/worker behavior, ABI, schema, runtime version, threshold, support status, architecture revision, or release artifact change. Runtime/sanitizer/platform results remain historical, not rerun.

### Verification before publication — 2026-09-18

| Exact command / check | Result |
| --- | --- |
| `& '.\scripts\docs\Test-DocumentationMirrors.ps1'` | Passed: 68 physical Markdown pairs, SHA-256 equality, UTF-8 without BOM, LF, local paths and DPE-ARCH-0017 revision agreement |
| Same script with `-ExternalDocsRoot "C:\Users\monyd\Documents\Monydragon Services\Shared\Monydragon Services Shared\Engines\Dragon Pixel Engine"` | Passed: all 68 pairs against the requested shared root |
| `python out/verify-documentation-010.py` | Passed: all 20 pre-existing level-two sections retained (Current Generated Result body relocated intact), original-prompt SHA above, required reading order, 83 local links/anchors across six changed Markdown files, balanced fences/details, valid UTF-8/LF; README 4,312 words |
| `git diff --check` | Passed; no whitespace errors |
| `gh pr view 6 --json state,mergedAt,mergeCommit,url` | Verified MERGED at `fa59b227e3057af603c569f1913b652d22b50c5a`, 2026-07-29T06:09:48Z |
| `git diff --name-only origin/feature/project-view-workflow` plus untracked-file review | Documentation-only scope; six intended child files including two new Markdown records |
| Byte comparison against `git show 8cbdf421f6baca785e9e730dd54e4a521857f77a:<path>` | LICENSE.md, Design Document, immutable first-structure prompt, and AGENTS.md unchanged |

The one-off Python verifier is a local ignored audit helper, not a new product test. Its preservation check splits the parent's Prompt Source on level-two headings, requires every original section verbatim in the new document (excluding only the relocated Current Generated Result heading), checks the original prompt precedes the result, and compares the four unchanged files byte-for-byte through `git show`. Link checks resolve relative destinations and GitHub-style heading anchors and inspect balanced code fences/details. Repository mirror validation is the checked-in reproducible full-document-tree gate. No runtime or sanitizer test was added or rerun for prose changes.

Reviewed README's aggregate diff, the relocated Prompt Source structure and preservation report, corrected Tilemap labels against GitHub, licensing text, six-file child scope, and parent commit sequence. No generated binaries, source changes, unrelated work, weakened tests, or new support claims are included.

Tag selection: use annotated `docs/v0.1.0` for the requested documentation baseline; the optional engine-snapshot alternative received no selection before publication. The annotation must say this contains unmerged parent work and does not qualify a product release. The tag will identify the completed content commit; a subsequent documentation-only publication receipt may follow on the branch without moving that tag.

Parent complete Ubuntu matrix remains blocked; macOS remains deferred/unrun for the parent; KNI is experimental. All slice, POC, ADR, packaging, and release gates remain unchanged. Publish the verified content, open a dependent draft PR, then record remote branch/tag identities and final handoff below.
