# Agent Guide

This repository is a multi-language ASCII word-frequency benchmark. Keep the
developer interface behind `mise`.

## Rules

- Use `~/dev/personal/standards` for stack standards and mirror its strictness
  when a language is not covered there.
- Use `~/dev/personal/tokenfreq-c99` as the correctness oracle.
- Do not add per-language test suites. Validate implementations against the
  oracle through `mise run validate`.
- Do not add CI: no Dagger setup, no `.github/workflows`, and no CI-only task
  surface.
- Do not reintroduce public shell scripts, direct compiler commands in docs, or
  root-level generated binaries.
- Keep implementations self-contained under their language directories.

## Commands

- `mise run tasks`
- `mise run fmt`
- `mise run fmt:check`
- `mise run lint`
- `mise run build`
- `mise run validate`
- `mise run bench`
- `mise run check:local`
