---
description: "Determine current phase progress and plan the next phase"
allowed-tools: [Read, Glob, Grep, Bash, Agent, EnterPlanMode]
---

## Context

You are planning the next compiler phase for the mini-compiler project.

### Current codebase state
- Git status: !`git status --short`
- Files in src/: !`find src/ -name '*.cpp' -o -name '*.h' -o -name '*.l' -o -name '*.y' 2>/dev/null || echo "(empty)"`
- Files in tests/: !`find tests/ -name '*.cpp' 2>/dev/null || echo "(empty)"`
- Files in lean/MiniCompiler/: !`find lean/MiniCompiler/ -name '*.lean' 2>/dev/null || echo "(empty)"`
- Existing changesets: !`ls .changeset/*.md 2>/dev/null | grep -v README || echo "(none)"`
- Recent commits: !`git log --oneline -10 2>/dev/null || echo "(no commits)"`

### Roadmap
!`cat ROADMAP.md`

## Instructions

1. **Determine current phase**: Look at existing source files, tests, and changesets to figure out which phase is complete or in progress. A phase is complete when all its checklist items are done (src files exist, tests pass, lean stubs exist).

2. **Identify next phase**: The next phase is the first one with incomplete checklist items. If a phase is partially done, continue it. If fully done, start the next.

3. **Enter plan mode** and write a plan for the next phase. The plan must follow the project's TDD cycle:
   - List the Hoare contracts (pre/postconditions) for each function to implement
   - List failing tests to write FIRST (RED) — unit tests + Z3 predicate tests
   - List Lean theorem stubs (sorry) to write [|| parallel with tests]
   - List implementation files to create (GREEN)
   - List integration tests and Lean proofs [|| parallel]
   - Mark parallelizable items with `[||]`

4. **Per-file detail**: For each file to create/modify, specify:
   - File path
   - What it contains (brief)
   - Which functions/types with their contracts

5. **Reference book pages**: Note which pages from the reference book (Siek 2023) the Researcher agent should read for this phase.

6. **Researcher look-ahead**: After the Researcher finishes extracting grammar/invariants/contracts for the current phase, it should also read ahead into the **next phase's** book pages and:
   - Extract upcoming grammar extensions, new AST nodes, new pass requirements
   - Identify dependencies between current phase decisions and next phase needs
   - Propose refinements to the next phase's ROADMAP.md section (new checklist items, adjusted contracts, edge cases discovered)
   - Write findings to a `## Phase N+1 Look-Ahead` section in the plan
   This runs `[||]` parallel with Writer+Prover starting on current phase work.

7. **Changeset**: Plan which `.changeset/NNN_theme.md` to create summarizing the phase.

8. **Verification**: How to verify the phase is complete (tests to run, lint check, lean build).

Keep the plan extremely concise. Sacrifice grammar for concision. Ask unresolved questions at the end.
