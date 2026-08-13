---
"mini-compiler": patch
---

Fix a miscompilation of multi-argument function calls.

`fn add(x: Int, y: Int) : Int { x + y }` applied as `add(20, 22)` returned
**40**. The bug had been present since Phase 6 and survived Phases 7 and 8 and a
287-case test suite.

A function's entry sequence is a parallel move performed sequentially:

```
movq %rdi, p0
movq %rsi, p1
movq %rdx, p2
```

Liveness analysis only tracks `VarArg`s — `var_from_arg` ignores physical
registers entirely — so it could not see that `%rdx` still held `p2` while `p1`
was being written. The allocator was therefore free to give `p1` the home
`%rdx`, and the third instruction then read a register the second had already
destroyed.

`assign_homes` now precolours: parameter `i` is given an interference edge
against every argument register still holding one of parameters `i+1..`, so it
can never be homed there. `X86FunctionDef` carries `params` to make this
possible.

The caller side has the same shape but is safe by construction: `IndirectCallq`
clobbers all caller-saved registers, so a variable live across the call already
interferes with every argument register. Confirmed over 1600 random programs.

Also adds `tests/run_differential.sh`, which runs every `tests/programs/**.mc`
through both the interpreter and the compiled binary and requires them to agree.
This is what found the bug, and it now runs in CI. Plus a regression test,
`Pipeline.FunctionEntryMovsDoNotClobber`, which asserts that no instruction in
the entry mov run reads a register an earlier one has written.

Two further fixes:
- `main` now gets `.align 8`, like every other entry point.
- `make_atom` in explicate_control ended in `default:` returning
  `expr_cast<VarExpr>(e)` for *any* node kind — a wrong-type cast, undefined
  behaviour in a release build. It now throws `ExplicateError` naming the kind.
