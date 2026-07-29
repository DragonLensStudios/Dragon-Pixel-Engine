# Dragon Pixel Engine Project View User Guide

> **Applies to:** DPE-ARCH-0017 Project Window workflow
> **Status:** Implemented and Project-specific QA passed on `feature/project-view-workflow`; draft PR #7 remains blocked by the complete Ubuntu gate
> **Last updated:** 2026-07-29

## Project View layout

Open **View > Project** if the Project dock is hidden. In the default two-column layout, Favorites and the project folder hierarchy appear on the left. The right side shows only the immediate folders and assets inside the active folder. Use the **Layout** menu to switch to one-column folder navigation, and use **Lock** when externally driven Project navigation should not replace the current location.

Use **Back**, **Forward**, and **Up** or select a segment in the clickable breadcrumb to navigate. Folder activation in the tree, list, or icon view changes the active folder. Built-in Favorites select all assets, Scenes, or Prefabs; folder favorites and saved searches are retained as per-user editor state. Missing favorites are ignored without changing project data.

Folders sort before assets. Names use deterministic, case-insensitive natural ordering, so `Folder2` appears before `Folder10`. The content surface normally uses scalable icon tiles. Move the bottom icon-size slider to its extreme left for the compact **Name** and **Kind / Type** list. Both modes project the same indexed records and keep stable selection synchronized; status and diagnostic metadata remain available in details and tooltips instead of consuming navigation columns.

The bottom row identifies the selected item and shows its full logical path while searching. The editor restores the nearest valid active folder, expanded folders, stable selected item, splitter position, layout, lock, and icon size after refresh. If a remembered folder was removed, Project View falls back to the nearest valid ancestor and then the declared Assets root. These choices are per-user editor state and never modify project or asset files.

## Search, saved searches, and keyboard

Search terms separated by whitespace are combined with AND. Prefix a term with `t:` to filter by indexed type; multiple `t:` terms are ORed. Prefix a term with `s:` to filter by Dragon Pixel status; multiple `s:` terms are ORed. Search traverses the project and temporarily uses the compact result list. Clear the search to return to the active folder's immediate children and the selected icon size. Use **Save** to place the current query in Favorites.

| Action | Keyboard |
| --- | --- |
| Focus Project search | Ctrl/Cmd+F |
| Move focus through Project controls | Tab / Shift+Tab |
| Frame the selected item | F |
| Select all visible items | Ctrl/Cmd+A |
| Duplicate the selected supported item | Ctrl/Cmd+D |
| Move the selected supported item to recoverable trash | Delete, with confirmation |
| Move to recoverable trash without a dialog | Shift+Delete |
| Rename on Windows | F2 |
| Open or activate the selection | Enter / Return |
| Navigate to the parent folder | Backspace |

## Organizing folders and assets

Create, import, rename, move, duplicate, trash, and restore operations continue through `AssetService`. Project View proposes an operation; it never moves project files directly.

A supported OS file drop imports into the validated visible folder. A Project asset or folder can be dragged to another visible Project folder even when no Scene is open. Successful moves preserve stable asset IDs. Recursive folder moves, collisions, stale revisions, cross-project payloads, project-root moves, and ambiguous multi-item mutations are rejected before authoritative mutation.

Use the Console and Project details area for rejection diagnostics. Refreshing after a failed operation must continue to show the last valid project index.

## Dragging into authoring surfaces

| Drag source | Destination | Result |
| --- | --- | --- |
| Sprite/image asset | Scene or Hierarchy | Creates the supported scene object through existing scene commands. |
| Tilemap asset | Scene, Hierarchy, or compatible Inspector field | Creates or assigns the existing Tilemap workflow through validated owners. |
| Prefab asset | Scene or Hierarchy | Creates a linked prefab instance through the prefab command path. |
| Compatible asset | Inspector asset field | Assigns the stable asset reference after type and revision validation. |
| Locally owned Hierarchy root | Project folder | Creates a linked prefab; this path requires a valid open Scene. |
| Asset or folder | Project folder | Moves it through `AssetService`; this path does not require a Scene. |

Scene placement remains deterministic at the currently supported default placement. Cursor/surface-specific 3D placement and multi-item atomic Project moves are not part of this feature.

## Working with multiple contributors

Independent work is preferred:

1. Fetch the latest `develop`.
2. Create one focused `feature/<descriptive-name>` branch.
3. Create a mirrored feature plan with an owner, scope, affected files/systems, and overlap notes.
4. Open a draft PR into `develop` and leave it for manual review.
5. Other contributors may create unrelated feature branches from current `develop`; PRs do not need to wait merely because another independent PR is open.

Use a dependent PR stack only when the child cannot be reviewed meaningfully without an unmerged parent:

1. Record the parent branch/PR, dependency, overlap, merge order, and owner in both feature plans and both PR bodies.
2. Temporarily target the child `feature/*` PR at the parent `feature/*` branch so CI evaluates the intended delta.
3. Never merge the child before the parent.
4. After the parent merges, update the child from current `develop`, retarget it to `develop`, review the aggregate diff, resolve conflicts, and rerun affected checks.
5. Do not force-push a teammate's shared branch without explicit coordination.

Every PR remains manually reviewed. A dependent base changes review topology only; it does not bypass validation, required evidence, branch protection, or final review against `develop`.

## Current verification boundary

DPE-ARCH-0017's changed Project aliases pass Windows Release 3/3 in 340.03 seconds, Windows MSVC AddressSanitizer 3/3 in 571.63 seconds without findings, and Ubuntu 24.04 Release 3/3 in 48.25 seconds. Native-Windows Qt renders cover two-column icons, two-column list, and one-column mode; public tests cover lock and the other interactions described above.

The current complete Ubuntu matrix is not stable enough for review handoff: three current-source runs each passed 61/62, first because of a sequence-sensitive crash-recovery allocator failure and then twice because KNI POC J measured 29.250 and 28.879 FPS against its unchanged 30.0 FPS threshold. Focused reruns passed, but the complete gate remains open. macOS is deferred for this feature but remains a required product platform gate. This feature changes no durable authoring format, drag-envelope version, C ABI, managed contract, worker protocol, POC status, ADR acceptance status, release status, or KNI production status.
