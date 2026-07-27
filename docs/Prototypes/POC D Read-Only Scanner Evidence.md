# POC D Read-Only MonoGame and KNI Scanner Evidence

> **Evidence status:** Windows and Ubuntu passed; macOS pending
> **Recorded:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0005`

## Purpose

Validate that Dragon Pixel Engine can inspect a MonoGame or KNI source tree out of process, produce useful migration evidence, and prove it did not modify the inspected project.

## Implemented proof

- A standalone .NET 10 scanner that does not load projects through MSBuild and does not execute inspected code.
- Secure XML parsing with DTD processing prohibited and no XML resolver.
- Detection of target frameworks, package references and versions, MonoGame/KNI family, MGCB files, common content-source extensions, and selected XNA API signals with file/line locations.
- Deterministic JSON and Markdown reports.
- A hard guard rejecting either report path when it is inside the inspected source tree.
- Independent test manifests comparing every input path, byte length, last-write timestamp, file attributes, and SHA-256 before and after the out-of-process scan.
- An independently recomputed whole-tree hash checked against the scanner's report.
- Explicit report warnings that inspection does not prove behavioral compatibility or convert gameplay intent.
- Explicit KNI experimental-status warning.

## Windows evidence

Environment: Windows 11 x64 and .NET SDK 10.0.203.

| Fixture | Detected framework | Input tree SHA-256 | JSON | Markdown | Pre/post manifest |
| --- | --- | --- | --- | --- | --- |
| MonoGame sample | MonoGame 3.8.5 | `91eab54044e512bb4f23fffac7cdfd4b58b264e279953d9953eea8601ff0419c` | Produced | Produced | Identical |
| KNI sample | KNI 4.2.9001.1 | `308a9aa75d306e091c1414243c3b83dc9dc40d114386aba76c45c053a60b1711` | Produced | Produced | Identical |

`poc_d.read_only_scanner` passed in both the Release and MSVC AddressSanitizer CTest suites. The containment-negative test also passed: an attempted report path under the source root was rejected and no file appeared there.

## Ubuntu evidence

`poc_d.read_only_scanner` passes in the clean Ubuntu 24.04 x64 Release and Clang AddressSanitizer suites. Both fixtures produce deterministic JSON/Markdown reports outside the inspected trees. Pre/post path, length, timestamp, attributes, per-file SHA-256, and independently recomputed whole-tree hashes remain identical, and the contained-output negative case remains rejected.

## Evidence boundaries

- Fixtures are intentionally small and synthetic. Real project structure, imported MSBuild props/targets, linked files, custom content processors, generated code, and source generators require additional scanner coverage.
- Package/API detection is evidence for a human-readable migration report, not proof of convertibility.
- The scanner currently identifies signals; it does not yet produce the richer confidence, unsupported-feature, dependency-graph, and remediation sections expected from a product migration analyzer.

## Remaining gate work

- Run the same no-write test on macOS 14+ arm64.
- Add representative real-world MonoGame and KNI fixture cases without checking proprietary sources into the repository.
- Add symlink/reparse-point, permission-denied, malformed XML, very large tree, cancellation, and custom-build-file tests.
- Carry the proven read-only boundary into Slice 3 migration analysis.

## Conclusion

The out-of-process, report-only scanner boundary is feasible on Windows and Ubuntu and has direct no-write evidence on both. macOS and real-project coverage remain required before POC D is considered fully closed.
