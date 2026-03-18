# mini-compiler

C++ compiler -> x86-64. Lean 4 proofs. Siek 2023.

## Build
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/src/mc input.mc -o output.s
gcc -o output output.s build/src/libmc_runtime.a -lm

## Run
./build/src/mc input.mc -o output.s      # compile
echo "42" | ./build/src/mc -i input.mc   # interpret

## Test
cd build && ctest --output-on-failure

## Lean
cd lean && lake build
Local deps: mathlib4 + cslib from `../../lean-proofs/` (no network fetch).

## Lint & format
- clang-tidy runs on every build (CMAKE_CXX_CLANG_TIDY). Fix all warnings before commit.
- `clang-format -i src/**/*.{h,cpp}` before commit (LLVM style).

## Changesets
Create `.changeset/NNN_theme.md` per phase commit or big change. evalite-style frontmatter:
`"mini-compiler": patch|minor|major` + user-facing description.

## Pipeline
```
Source (.mc) -> [Lexer] -> [Parser] -> AST
  -> [Shrink] -> [Uniquify] -> [Uncover Get] -> [Expose Allocation]
  -> [RCO] -> [Explicate Control] -> C_Var IR
  -> [Select Instructions] -> [Assign Homes] -> [Patch Instructions]
  -> [Prelude/Conclusion] -> [Emit] -> x86-64 AT&T assembly (.s)
```

## Structure
```
src/              lexer, parser, ast, interpreter, type.h/.cpp, type_checker
src/passes/       one .h/.cpp per pass (uniquify, expose_allocation, rco, ...)
src/ir/           c_ir.h (basic blocks), x86_ir.h (pseudo-x86)
runtime/          runtime.c (read_int, print_int, GC: initialize, collect)
tests/unit/       Google Test per-pass: test_{pass}.cpp
tests/integration/ end-to-end pipeline tests
tests/programs/   .mc files in phaseN/ subdirs
lean/             lakefile.lean (local mathlib4+cslib), MiniCompiler/*.lean
.changeset/       per-commit changelogs
```

## Conventions
- AST: class hierarchy, virtual dispatch, std::unique_ptr (no raw new/delete)
- Passes: take AST/IR ref, return new AST/IR (functional style, no mutation)
- Naming: snake_case functions/vars, PascalCase types/classes

## C++ coding rules
- Functions/methods < 30 lines (except enum-switch FSMs)
- No recursive functions — use iterative alternatives
- No global/module variables (except imports)
- For/while loop bodies < 30 lines (except enum-switch FSMs)
- Every function has Lean4-style contract docstring (requires/ensures/invariant/modifies/fresh/reads). Omit N/A sections.
- Above each for/while: `// decreases $leanExpr` (while) + `// invariant $leanExpr`

## TDD cycle (Hoare logic discipline)
1. Define Hoare contracts: preconditions/postconditions FIRST
2. Write failing Google Test (RED) + Z3 predicate tests
3. Write Lean theorem stub (sorry) [|| parallel with 2]
4. Implement correct-by-construction C++ (GREEN)
5. Prove Lean theorem [|| with integration tests]
6. Reviewer gate

## Agent workflow
1. Researcher: read book pages, extract grammar + invariants + contracts
2. Writer + Prover [||]: contracts first, then failing tests + Z3 + Lean stubs
3. Writer: correct-by-construction C++ (GREEN)
4. Prover + Writer [||]: Lean proofs + integration tests
5. Reviewer: contracts match code, Z3 passes, proofs, quality

## Lean conventions
- Mirror C++ AST as inductive types in MiniCompiler/AST.lean
- Each pass: Lean function + correctness theorem, sorry = tracked placeholder
- Structural recursion preferred; minimal Mathlib + CSlib imports
