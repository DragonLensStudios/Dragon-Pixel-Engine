# Commit Guide

## Format

`type(scope): imperative summary`

## Types

- `feat`
- `fix`
- `refactor`
- `test`
- `build`
- `ci`
- `docs`
- `perf`
- `chore`

## Good examples

- `test(publication): reproduce backup cleanup interruption`
- `fix(publication): keep prior manifest until candidate validation`
- `refactor(editor): extract reusable selection service`
- `ci(gitflow): validate pull requests into develop`
- `docs(governance): clarify public documentation authority`

## Rules

- One understandable purpose.
- No unrelated formatting.
- Behavior changes include tests.
- Refactors preserve behavior and have verification.
- Generated files are committed only when repository policy requires them.
- Do not use `update`, `changes`, `fixes`, `work`, or `final`.
