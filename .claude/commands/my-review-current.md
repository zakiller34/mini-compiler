---
description: "Review recent work: verify correctness, contracts, tests, proofs, lint"
allowed-tools: [Read, Glob, Grep, Bash, Agent, LSP]
---

## Context

You are reviewing the most recent work done on the mini-compiler project.

### Recent changes
- Recent commits: !`git log --oneline -5 2>/dev/null || echo "(no commits)"`
- Changed files: !`git diff --name-only HEAD~1 HEAD 2>/dev/null || git diff --name-only --cached 2>/dev/null || echo "(no changes)"`
- Unstaged changes: !`git diff --name-only 2>/dev/null || echo "(none)"`
- Untracked files: !`git ls-files --others --exclude-standard 2>/dev/null || echo "(none)"`

### Current codebase
- Source files: !`find src/ -name '*.cpp' -o -name '*.h' 2>/dev/null || echo "(empty)"`
- Test files: !`find tests/ -name '*.cpp' 2>/dev/null || echo "(empty)"`
- Lean files: !`find lean/MiniCompiler/ -name '*.lean' 2>/dev/null || echo "(empty)"`

### Project conventions
!`cat CLAUDE.md`

## Instructions

Launch the following review agents in parallel where possible.

### Agent 1: Contract & Code Review
- Read every recently changed/created `.cpp` and `.h` file
- Verify every function has Lean4-style contract docstring (requires/ensures/invariant/modifies/fresh/reads)
- Verify no function exceeds 30 lines (except enum-switch FSMs)
- Verify no recursion, no globals, no loop body > 30 lines
- Verify loop annotations present (decreases/invariant)
- Check naming: snake_case functions/vars, PascalCase types/classes
- Check passes are functional (take ref, return new AST/IR, no mutation)
- Use LSP diagnostics to catch type errors or missing imports
- List every violation found with file:line

### Agent 2: Test Coverage Review `[||]`
- Read every recently changed/created test file
- Verify unit tests exist for each new/modified pass (tests/unit/test_{pass}.cpp)
- Verify Z3 predicate tests exist for pass invariants (tests/z3/)
- Check tests actually assert the Hoare postconditions from the contracts
- Run `cd build && ctest --output-on-failure` if build dir exists — report failures
- List missing test coverage: which functions/invariants lack tests

### Agent 3: Lean Proof Review `[||]`
- Read every recently changed/created `.lean` file
- Verify each C++ pass has a corresponding Lean function + theorem stub
- Check for sorry count — list each sorry with location
- Verify AST.lean mirrors current C++ AST nodes
- Run `cd lean && lake build` if lakefile exists — report errors
- List missing theorems: which pass invariants lack Lean stubs

### Agent 4: Lint & Format Check `[||]`
- Run `clang-format --dry-run -Werror src/**/*.{h,cpp} 2>&1` if source files exist — report formatting violations
- Run `cmake --build build 2>&1 | grep -i 'warning\|error'` if build dir exists — report clang-tidy findings
- Check .changeset/ has an entry for the recent work

## Output

After all agents complete, produce a single consolidated report:

```
## Review Summary

### Passed
- (list what's correct)

### Issues Found
1. [severity: high|medium|low] file:line — description
2. ...

### Missing
- (missing tests, contracts, lean stubs, changeset)

### Suggested Fixes
- (concrete actions to resolve each issue)
```

Be extremely concise. No fluff. Just findings and fixes.
