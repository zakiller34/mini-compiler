# mini-compiler

C++ compiler targeting x86-64, built incrementally following [*Essentials of Compilation*](https://mitpress.mit.edu/9780262047760/) by Jeremy G. Siek (MIT Press, 2023). The book uses Racket; this project reimplements everything in C++17 with Lean 4 correctness proofs.

The source language (`.mc` files) uses C-like concrete syntax. Each phase adds language features: integers & variables, register allocation, booleans & conditionals, loops, tuples & GC, functions, closures, dynamic typing, gradual typing.

**Current status: Phase 1 (L_Var) complete** — integers, variables, `let` bindings, `read()`, full nanopass pipeline to x86-64.

## Example

```
let x = read();
let y = read();
x + -(y)
```

With input `52` and `10`, produces `42`.

## Reference

Siek, J. G. (2023). *Essentials of Compilation: An Incremental Approach in Racket*. MIT Press. ISBN 9780262047760.

See [ROADMAP.md](ROADMAP.md) for per-phase plan and progress.
