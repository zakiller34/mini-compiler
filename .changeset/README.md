# Changesets

Track user-facing changes per phase commit or significant change.

## Naming

`NNN_theme-of-change.md` — e.g. `001_project-scaffold.md`, `002_phase1-frontend.md`

## Format

```md
---
"mini-compiler": patch
---

Description of what changed (user-facing).
```

Use `patch` for fixes/small changes, `minor` for new features/phases, `major` for breaking changes.
