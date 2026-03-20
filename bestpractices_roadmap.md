# Best Practices Audit — Roadmap

## Audit Summary

### What's Already Good
- Smart pointers (`unique_ptr`/`shared_ptr`) — no raw `new`/`delete`
- Move semantics — consistent `std::move()` everywhere
- Const correctness — const params, const methods, const overrides
- RAII — exception-based error handling, typed exceptions (`ParseError`, `TypeError`)
- No recursion — iterative stack-based passes
- No `using namespace std;` in headers
- `#pragma once` everywhere, clean includes, forward declarations
- clang-tidy on every build, clang-format enforced
- Google Test + Z3 predicate tests + integration tests
- No global mutable state in C++ core (only `runtime.c`)

### Gaps Found

## Phase 1: Bug Fixes + Quick Wins — DONE

- [x] Global mutable state — `expose_allocation.cpp:10` — `tmp_counter` → local `int&` param
- [x] Silent error — `expose_allocation.cpp:60` — `infer_vector_type` default → `assert(false)`
- [x] Magic numbers — `prelude_conclusion.cpp` — `kHeapSize`, `kRootstackSize`, `kWordSize` constexpr
- [x] Magic numbers — `assign_homes.cpp` — `kWordSize`, `kAlignment` constexpr
- [x] Magic numbers — `select_instructions.cpp` — `kWordSize` constexpr
- [x] Magic numbers — `expose_allocation.cpp` — `kWordSize` constexpr

## Phase 2: Safety + Expressiveness — IN PROGRESS

- [x] Unsafe downcasts — 212× `static_cast` → `expr_cast<T>()` with debug assert across all passes
- [x] No `std::optional` — `main.cpp` `parse_file` returns `std::optional<Program>`
- [ ] Manual index loops — ~48 `for(size_t i=0;...)` → range-for / algorithms where clear win

## Phase 3: Namespace Wrap

- [ ] No namespaces — all AST types, passes, lexer, parser → `namespace mc {}`

## Phase 4: DRY Refactor

- [ ] ~18 frame structs duplicated across 6+ passes (~500 lines) → extract shared frame structs
- [ ] Common `push_eval` leaf cases duplicated → factor common leaf cases

## Phase 5: Process

- [ ] No CI — GitHub Actions: build + test + lint
- [ ] 1 structured binding — add structured bindings in map iterations

## Deferred (Low Priority)

- Public AST fields — acceptable: data-oriented AST, functional passes
- C++17 not C++20 — no template code needing concepts
- Strong types for bare `int` — low risk in this codebase
