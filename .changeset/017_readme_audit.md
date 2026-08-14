---
"mini-compiler": patch
---

README rewritten around an audited, measured claim; two supporting fixes so every
claim has a checked-in artifact behind it.

The previous README's narrative could be read as "formal verification found seven
bugs in this compiler". Git says otherwise: `any_rebuild.h` shipped in `19c0903`,
the same commit that introduced the `L_Any` nodes, and the proof-hygiene commit
`982c882` touched 22 files and none under `src/`. The seven catch-all bugs were in
the Lean *model*; the C++ never had them. The README now leads with the measured
result instead — Lean found 0 live compiler bugs, the 77-line differential harness
found the one a 287-case suite missed, Z3 proved a one-bit design constraint no
test can state, and the proof-hygiene gate caught that 20 of 35 theorems asserted
nothing — and positions the syntactic tier against CompCert/Csmith.

Claims corrected against the code:

- `#eval`/`#print axioms` were quoted as a falsifiability "receipt" but existed
  nowhere in `lean/`. Now checked in as real theorems (below) and quoted from there.
- "Confirmed by fuzzing 1,600 random programs" — no fuzzer exists in the repo. Cut;
  the caller side's safety is argued from `IndirectCallq`'s clobber set instead.
- 33 theorems / 30 proved → 36 / 33 (`Ty.beq_self` is `@[simp] theorem`, missed by a
  `^theorem` count). "Eleven root-namespace theorems" → twelve. Lean lines 1,750 →
  1,776, itemised as model + hygiene + lakefile.
- "Hoare contracts on every function" → roughly three-quarters, with the uncovered
  files named. Contracts are documentation, not a tool-checked regime; the
  who-caught-what table no longer credits `assign_homes.cpp` with a postcondition it
  does not have.
- "The interpreter and compiled binary share only the lexer, parser and type
  checker" — false under `--dyn`, where they also share shrink, uniquify,
  reveal_functions and cast_insert.
- The liveness/interference/colouring "proved" row narrowed: nothing is proved about
  the liveness fixpoint, `liveness_sound` is an alias, and `dsatur_terminates` is
  `omega` on Nat subtraction with no DSATUR in the file.
- Three code snippets that had drifted (deleted loop annotations, an added comment,
  a shortened assertion message) now quote their sources byte-for-byte.
- New "Known gaps" section: clang-format drift that CI does not fail on, no
  model↔C++ link, the dead `num_regs` parameter, `main.cpp`'s in-place mutation.

Additions the codebase had earned but the README never mentioned: the compiler is
recursion-free by construction (every pass is an explicit stack machine, so it
cannot overflow on a deeply nested program), `any_rebuild.h` makes the missed-node
bug class unrepresentable, 176 loop variant/invariant annotations, and the
`make_atom` catch-all that *was* undefined behaviour in a release build — the same
bug shape as the model's, in a pass nobody modelled.

- `lean/MiniCompiler/Passes/Shrink.lean`: adds `noAndOr_is_falsifiable` and
  `shrink_clears_and_or_under_inject`, so the witness that `shrink_no_and_or` is
  not vacuous is elaborated by every build rather than asserted in prose.
- `tests/run_differential.sh`: header said "twenty-one passes"; now twenty, and
  states the `--dyn` limitation on fault localisation.
