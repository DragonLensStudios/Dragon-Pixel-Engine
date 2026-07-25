# POC H Qt Editor Interaction Evidence

> **Evidence status:** Expanded Windows and Ubuntu Qt interaction matrices passed; Windows manual operability passed; complete accessibility/device workflows and macOS matrix pending  
> **Recorded:** 2026-07-25  
> **Architecture baseline:** `DPE-ARCH-0006`  
> **Related ADRs:** ADR-0005, ADR-0007, and ADR-0008

## Purpose

Prove that Slice 2 editor behavior is exercised through real public Qt widgets, models, events, dialogs, shortcuts, focus, and accessibility surfaces instead of direct calls to private implementation handlers.

## Implemented proof

- Model-backed Hierarchy, Project Explorer, Inspector, and structured Console views.
- Real Add GameObject menu interaction through `QToolButton` and `QMenu`.
- GameObject creation, title dirty marker, Ctrl+Z Undo, Ctrl+Y Redo, hierarchy search, row selection, Delete, and injectable deletion confirmation.
- Enabled action-state checks for Undo, Save, and built-in 2D/3D/Debug workspaces.
- Typed Inspector delegate coverage for JSON-safe string and enum editing, including quotes and backslashes.
- Move/Rotate/Scale gizmo preview transactions for ordered multi-selection, local/global orientation, snapping, mouse-release commit as one Undo item, and Escape restoration.
- Candidate-backed Project Explorer hierarchy with type/status filters, refresh, watcher-triggered rebuilds, stable selection roles, asynchronous sprite/mesh thumbnails, and stale-generation suppression.
- Structured Console columns for time, severity, subsystem, worker, session, correlation, context, and message, plus Clear, Copy, CSV export, and entity/asset navigation roles.
- Production command validation exercised through the Inspector: an out-of-range value is rejected without dirtying the project or creating Undo history, while a valid value commits normally.
- Accessible names on the editor, hierarchy, Inspector, Project Explorer, Console, viewport, and primary controls.
- Worker/editor tests for both adapters, play-worker crash recovery, active-worker picking, adapter locking during Play, and frame/socket cleanup.

## Windows evidence

Environment: Windows 11 x64, Qt 6.11.1, and MSVC v143 with `/W4 /WX`.

The complete Windows Release matrix passed **36/36 tests in 118.39 seconds**. The complete MSVC AddressSanitizer matrix passed **36/36 tests in 134.93 seconds**. The passing editor/model tests include:

- `s1.editor_monogame`
- `s1.editor_kni`
- `s1.editor_crash_recovery`
- `s2.editor_interactions`
- `poc_h.qt_interactions`
- `s2.project_index_service`
- `s2.asset_preview_service`
- `s2.project_model_candidate`
- `s2.console_model_structured`
- `s2.command_validation`
- `s2.prefab_editor_service`
- `poc_f.prefab_editor_workflows`

The Qt interaction executable opens the real editor against a disposable project, clicks the Add GameObject menu, sends keyboard shortcuts and search/filter text, changes model selection, deletes through the public key event and prompt seam, edits typed delegates, exercises gizmo preview/commit/cancel, checks project thumbnails and status/type filtering, rejects and then accepts Inspector values through command validation, copies/clears/navigates the structured Console, and checks exposed action/accessibility state. Test shutdown leaves no newly created worker or shared-frame files.

Manual Windows QA launched only the disposable writable `out/dev/Slice1Sample` copy. It visually confirmed real MonoGame and KNI preview/play frames, typed Inspector editing, preset creation as one transaction followed by Undo, isolated Simulate Preview, and KNI pause/stop. This was an operability observation, not a substitute for automated or assistive-technology evidence.

## Ubuntu evidence

The same Qt editor/model aliases pass in the complete Ubuntu matrices under Xvfb: Release passed **36/36 tests in 102.20 seconds**, and Clang AddressSanitizer passed **36/36 tests in 101.97 seconds**. The matrix includes real MonoGame/KNI worker sessions, graphics-thread one-pixel picking, persistent shared-frame reading, crash recovery, project/index/preview models, structured Console, prefab services, physics, and POC H interaction tests. Sanitizer settings propagate through managed workers and native-host child processes.

## Evidence boundaries

The current POC H executable is materially broader than the initial subset, but it still does not prove the complete requested matrix. Hierarchy multi-drag/reorder and deletion-reference repair remain partial. Project-to-scene drag/drop and compatible asset assignment need full device workflow coverage. Inspector entity/asset choosers, component-card reorder, specialized/custom drawers, save-discard-cancel dialogs, full focus traversal, selection outline/editor-camera persistence, complete prefab/physics workflows, and workspace restoration across restart remain incomplete or insufficiently exercised.

Accessible names and keyboard actions are present for the tested controls, but the broader high-DPI, high-contrast, keyboard-only, screen-reader role/action, and device workflow matrix remains open. Qt offscreen automation and one Windows manual pass are not substitutes for final Windows, macOS, and Linux accessibility/designer review. No current POC H run exists on macOS.

## Remaining gate work

- Extend Qt Test to the remaining drag/drop, dialog, focus, component, prefab, physics-authoring, layout-persistence, and accessibility flows.
- Extend gizmo evidence beyond the current projected test harness to complete camera/device interaction and selection-outline workflows.
- Run the identical Release and sanitizer-compatible matrix on macOS 14+ arm64.
- Complete high-DPI, high-contrast, keyboard-only, and platform assistive-technology review.
- Review deployment/license artifacts before accepting ADR-0007.

## Conclusion

The expanded public-widget/model interaction slice passes Windows and Ubuntu Release/native AddressSanitizer matrices, and the manual Windows operability pass is recorded. Preset transactions, undo/redo, typed validation, gizmo commit/cancel, project previews/filters, structured Console actions, worker recovery, and basic accessibility exposure are covered. POC H remains open until the incomplete designer/accessibility/device workflows and macOS matrix pass.
