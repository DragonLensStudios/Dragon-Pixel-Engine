# POC A Native/Managed ABI Evidence

> **Evidence status:** Windows and Ubuntu passed; macOS pending
> **Recorded:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0005`
> **Related ADR:** ADR-0003

## Purpose

Validate the proposed `dpe_api_v1` ownership, versioning, allocation, and error boundary between a C++20 shared library and a .NET 10 worker before production modules depend on it.

## Implemented proof

- A version-negotiated function table with capability flags and rejection of unsupported API versions.
- Opaque runtime, world, entity, and native-component handles.
- Explicit parent/child lifetimes with stale-handle rejection.
- UTF-8 pointer/length input and two-call output-buffer sizing.
- A paired native tracked allocator/free function with foreign-pointer and double-free rejection.
- Stable status values and a thread-local structured error record.
- Conversion of a forced native C++ exception into status/error data.
- A managed callback trampoline that contains a forced managed exception.
- Generated `LibraryImport` loading, typed function pointers, and `SafeHandle` wrappers.
- 10,000 managed create/use/destroy ownership cycles with a zero-live-object assertion.

## Windows evidence

Environment: Windows 11 x64, MSVC v143 from Visual Studio Build Tools 2022 17.14.37, .NET SDK 10.0.203.

| Configuration | `poc_a.native` | `poc_a.managed` | Result |
| --- | --- | --- | --- |
| Release | Passed | Passed, including 10,000 cycles | Passed |
| MSVC AddressSanitizer | Passed | Passed, including 10,000 cycles | Passed |

Commands were run through the checked-in CMake/CTest presets:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure

cmake --preset windows-msvc-asan
cmake --build --preset windows-asan
ctest --preset windows-asan --output-on-failure
```

The final Release and AddressSanitizer runs each executed both ABI tests through CTest with no failures.

## Ubuntu evidence

Environment: clean Ubuntu 24.04 x64 Docker image, Clang 18, Qt 6.11.1, and .NET SDK 10.0.203.

| Configuration | `poc_a.native` | `poc_a.managed` | Result |
| --- | --- | --- | --- |
| Release | Passed | Passed, including 10,000 cycles | Passed |
| Clang AddressSanitizer | Passed | Passed with the ASan runtime preloaded into the managed host, including 10,000 cycles | Passed |

The managed test prebuilds its worker and uses CTest-scoped ASan runtime injection only while loading the instrumented native library. Native executable tests retain their normal LeakSanitizer behavior.

## Remaining gate work

- Run the same Release and sanitizer test presets on macOS 14+ arm64.
- Review the evidence before accepting ADR-0003.
- Review the production `dpe_api_v1` module alongside the evidence; the dedicated POC harness remains test evidence rather than a public implementation module.

## Conclusion

The proposed boundary is feasible on Windows and Ubuntu, and its critical ownership/error paths have executable coverage in both normal and sanitizer configurations. POC A and ADR-0003 remain open until macOS evidence is recorded and reviewed.
