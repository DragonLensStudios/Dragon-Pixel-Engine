# Verification Checklist

Use the portions relevant to the feature.

## Build

- [ ] Clean configure
- [ ] Incremental build
- [ ] Strict warnings
- [ ] No unexpected generated changes

## Native

- [ ] Focused Release tests
- [ ] Focused AddressSanitizer tests
- [ ] Ownership and destruction paths
- [ ] Exception containment

## Managed

- [ ] Contract tests
- [ ] Worker lifecycle tests
- [ ] Disposal and exception paths
- [ ] Adapter-specific results separated

## Serialization

- [ ] Known record round trip
- [ ] Unknown record round trip
- [ ] Migration fixture
- [ ] Atomic save/recovery
- [ ] Invalid input rejection

## Qt

- [ ] Public UI interaction
- [ ] Keyboard navigation
- [ ] Focus behavior
- [ ] Accessible names/roles
- [ ] Undo/redo
- [ ] Prompt cancellation
- [ ] Model identity stability

## Workers

- [ ] Start
- [ ] Reload
- [ ] Pause/resume
- [ ] Stop
- [ ] Crash recovery
- [ ] Resource cleanup
- [ ] Runtime state does not write into authoring files

## Rendering and Input

- [ ] Real framework device output
- [ ] Scene-dependent pixels
- [ ] Picking
- [ ] Resize
- [ ] Input consumption
- [ ] Input-caused pixel/pick change
- [ ] Latency threshold

## Packaging

- [ ] Relocatable
- [ ] No source-tree absolute paths
- [ ] Required dependencies included
- [ ] Manifest hashes verify
- [ ] Packaged self-test
- [ ] Launch outside repository tree
- [ ] Licenses/notices included

## Platforms

- [ ] Windows Release
- [ ] Windows ASan
- [ ] Ubuntu Release
- [ ] Ubuntu ASan
- [ ] macOS Release
- [ ] macOS sanitizer
- [ ] Unsupported or unrun results stated honestly

## CI evidence

- [ ] Workflow syntax and action semantics validated
- [ ] GitFlow event source/target relationship accepted
- [ ] Native command failures propagate to the job result
- [ ] JUnit evidence exists and is readable
- [ ] JUnit contains at least the repository-defined minimum tests
- [ ] JUnit reports zero failures and zero errors
- [ ] Platform and configuration results reported separately
- [ ] Candidate required-check names are unique, stable, and observed

## Documentation

- [ ] Feature plan current
- [ ] README/status current
- [ ] ADR updated if needed
- [ ] Known limitations current
- [ ] Exact evidence recorded
- [ ] Handoff notes complete
