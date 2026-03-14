# mini-compiler

C++ compiler targeting x86-64, built incrementally following [*Essentials of Compilation*](https://mitpress.mit.edu/9780262047760/) by Jeremy G. Siek (MIT Press, 2023). The book uses Racket; this project reimplements everything in C++17 with Lean 4 correctness proofs.

The source language (`.mc` files) uses C-like concrete syntax. Each phase adds language features: integers & variables, register allocation, booleans & conditionals, loops, tuples & GC, functions, closures, dynamic typing, gradual typing.

**Current status: Phase 1 (L_Var) complete** — integers, variables, `let` bindings, `read()`, full nanopass pipeline to x86-64.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Run

```sh
# Compile to assembly
./build/src/mc input.mc -o output.s
gcc -o output output.s build/src/libmc_runtime.a -lm

# Interpret directly
echo "42" | ./build/src/mc -i input.mc
```

## Test

```sh
cd build && ctest --output-on-failure
```

## Example

```
let x = read();
let y = read();
x + -(y)
```

With input `52` and `10`, produces `42`.

## Compiler Pipeline

```
Source (.mc) -> [Lexer] -> [Parser] -> AST
  -> [Uniquify] -> [RCO] -> [Explicate Control] -> C_Var IR
  -> [Select Instructions] -> [Assign Homes] -> [Patch Instructions]
  -> [Prelude/Conclusion] -> [Emit] -> x86-64 AT&T assembly (.s)
```

## Project Structure

```
src/                  Lexer, parser, AST, interpreter
src/ir/               C_Var IR (basic blocks), x86 IR (pseudo-x86)
src/passes/           One .h/.cpp per pass (uniquify, rco, explicate, ...)
runtime/              runtime.c (read_int, print_int)
tests/unit/           Google Test per pass
tests/integration/    End-to-end pipeline tests
tests/programs/       .mc test files (phaseN/ subdirs)
lean/                 Lean 4 correctness proofs (sorry stubs)
```

## Conventions

- All AST/IR traversals are iterative (no recursion) using explicit stacks
- Functions < 30 lines (enforced by clang-tidy)
- Passes are functional: take input, return new output (no mutation)
- Lean4-style contract docstrings on every function

## Lean

```sh
cd lean && lake build
```

Requires local mathlib4 + cslib at `../../lean-proofs/`.

## Reference

Siek, J. G. (2023). *Essentials of Compilation: An Incremental Approach in Racket*. MIT Press. ISBN 9780262047760.

See [ROADMAP.md](ROADMAP.md) for per-phase plan and progress.
