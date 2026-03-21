# Compiler Roadmap

Based on *Essentials of Compilation* (Siek, 2023). Each phase adds language features incrementally.

---

## Phase 0: Project Scaffold & Tooling ✅

- [x] CMake build system with Google Test + clang-tidy
- [x] `.clang-tidy` + `.clang-format` configs
- [x] `.mcp.json` (context7 MCP server)
- [x] `.changeset/` folder with convention README
- [x] `lean/lakefile.lean` with local mathlib4 + cslib deps
- [x] `lean/lean-toolchain` matching v4.29.0-rc1
- [x] AST node base class hierarchy (`Expr`, `Program`)
- [x] Pretty-printer / AST dump (iterative S-expr format)
- [x] `runtime.c` (`read_int`, `print_int`, links with generated asm)

---

## Phase 1: Integers & Variables (Book Ch. 2, pp. 11-32) ✅

**Language: L_Var** — `int` literals, `read()`, unary `-`, binary `+`/`-`, `let` bindings.

### 1A — Frontend ✅

- [x] Hand-written lexer + recursive-descent parser (C-like syntax: `let x = e; body`, `read()`)
- [x] AST nodes: `IntExpr`, `VarExpr`, `ReadExpr`, `UnaryExpr`, `BinaryExpr`, `LetExpr`, `Program`
- [x] Iterative interpreter (explicit stack, no recursion)
- [x] All traversals use Eval/Apply pattern with explicit stacks

### 1B — Compiler Passes ✅

- [x] **uniquify** — alpha-rename variables (`x` → `x.1`, `x.2`)
- [x] **remove_complex_operands** — A-normal form; all operands atomic
- [x] **explicate_control** — AST → C_Var IR (basic blocks)
- [x] **select_instructions** — C_Var → pseudo-x86
- [x] **assign_homes** — vars → stack slots (`-8(%rbp)`, `-16(%rbp)`, ...)
- [x] **patch_instructions** — fix two-memory-operand via `%rax`
- [x] **generate_prelude_conclusion** — `.globl main`, frame setup/teardown
- [x] **emit** — AT&T syntax assembly output
- [x] CLI driver: `mc input.mc -o output.s` / `mc -i input.mc`
- [x] End-to-end: 12/12 `.mc` programs compile → assemble → link → correct output

### 1 — Tests ✅

- [x] `test_parser.cpp` — AST dump() verification
- [x] `test_interpreter.cpp` — interpret with manual ASTs
- [x] `test_uniquify.cpp` — no shadowing after rename
- [x] `test_rco.cpp` — all operands atomic
- [x] `test_explicate.cpp` — valid C_Var IR structure
- [x] `test_select_instructions.cpp` — correct x86 opcodes
- [x] `test_assign_homes.cpp` — no VarArg remains
- [x] `test_patch.cpp` — no two-memory-operand violations
- [x] `test_pipeline.cpp` — full integration test
- [x] 12 `.mc` programs in `tests/programs/phase1/`

### 1 — Lean Stubs (sorry'd)

- [x] `AST.lean` — L_Var inductive types mirroring C++ AST
- [x] `Passes/Uniquify.lean` — sorry stub
- [x] `Passes/RCO.lean` — sorry stub
- [x] `Passes/ExplicateControl.lean` — C_Var IR types + sorry stub
- [x] `Passes/SelectInstructions.lean` — sorry stub

### 1 — Deferred

- [ ] Z3 predicate tests (skipped for Phase 1)
- [ ] Lean proofs (sorry stubs only, real proofs TBD)

---

## Phase 2: Register Allocation (Book Ch. 3, pp. 33-53) ✅

Replace naive stack allocation with graph-coloring register allocator.

- [x] **liveness analysis** — backward pass over instructions; compute live-after sets per instruction
- [x] **build interference graph** — undirected graph: edge between vars live at same time; special handling for `movq` (no edge if src == dst)
- [x] **graph coloring** (DSATUR / saturation-based) — map variables to colors; colors 0..12 map to allocable registers, rest spill to stack
- [x] Register mapping: `rcx, rdx, rsi, rdi, r8-r10` (caller-saved) + `rbx, r12-r14` (callee-saved); reserved: `rax, rsp, rbp, r11, r15`
- [x] Update **assign_homes** to use coloring result
- [x] Update **patch_instructions** for register-to-register moves
- [x] Update **prelude/conclusion** to save/restore only used callee-saved registers
- [x] **Challenge: move biasing** — prefer assigning same color to src/dst of `movq`
- [x] Test: existing suite still passes; verify fewer stack accesses

### 2 — Tests (TDD) ✅

- [x] `test_liveness.cpp` — live-after sets correct for known instruction sequences
- [x] `test_interference.cpp` — edges present/absent for expected var pairs
- [x] `test_coloring.cpp` — valid coloring (no adjacent same color), minimal spills
- [x] Integration: phase 1 programs still produce correct output

### 2 — Z3 Predicate Tests ✅

- [x] `valid_coloring(graph, colors)` — ForAll (u,v) in edges, `color(u) != color(v)`
- [x] `liveness_covers_uses(live_sets, instrs)` — ForAll v k, `used_later(v,k) => v in live_after(k)`

### 2 — Lean Proofs ✅

- [x] `liveness_sound` — every used variable is in the live-after set of its definition point
- [x] `interference_correct` — edge iff two vars simultaneously live
- [x] `coloring_valid` — no two adjacent nodes share a color
- [x] `dsatur_terminates` — DSATUR loop terminates (uncolored count decreases)

---

## Phase 3: Booleans & Conditionals (Book Ch. 4, pp. 55-79) ✅

**Language: L_If** — extends L_Var with `bool`, `if/else`, comparisons, logic ops.

### 3A — Frontend ✅

- [x] New tokens: `TRUE`, `FALSE`, `IF`, `ELSE`, `AND`, `OR`, `NOT`, `EQ` (`==`), `LT` (`<`), `LE` (`<=`), `GT` (`>`), `GE` (`>=`), `{`, `}`
- [x] Grammar: `if (cond) { then } else { else }`, boolean literals, comparison/logic exprs
- [x] AST nodes: `BoolExpr`, `IfExpr`; extended `UnaryOp::Not`, `BinaryOp::And/Or/Eq/Lt/Le/Gt/Ge`
- [x] **Type checker** — two types: `Int`, `Bool`; enforce operand types; `if` branches same type; condition must be `Bool`; `==` works on both Int and Bool
- [x] Interpreter: extend with `variant<int64_t, bool>` values, short-circuit `and`/`or`

### 3B — Compiler Passes ✅

- [x] **shrink** — desugar `and`/`or` to `if`: `(and a b)` → `(if a b false)`, `(or a b)` → `(if a true b)`
- [x] Update **uniquify** for `if` and bool
- [x] Update **remove_complex_operands** — `if` condition in `Need::Expr`; `not`/`cmp` operands `Need::Atom`
- [x] **explicate_control** — produce C_If IR with `Tail` variant (`Return`/`Goto`/`IfStmt`); `explicate_pred` for predicate context; worklist-based block generation
- [x] **select_instructions** — `cmpq`, `setCC`, `movzbq` for comparisons; `xorq $1` for `not`; `jCC`/`jmp` for control flow; booleans encoded as 0/1
- [x] Liveness analysis across multiple basic blocks (topo-sort on DAG CFG)
- [x] Update interference graph for `movzbq`, `Cmpq`, `SetCC`, `Xorq`, `JmpIf`
- [x] Update `patch_instructions` for `cmpq` imm-dst fix
- [x] Update `emit` for new instructions + multi-block emission order
- [ ] **Challenge: optimize blocks** — remove trivial jumps, merge blocks
- [x] End-to-end: 6 phase3 .mc programs compile correctly; 12 phase1 programs still pass

### 3 — Tests ✅

- [x] `test_type_checker.cpp` — accept well-typed, reject ill-typed programs (21 tests)
- [x] `test_shrink.cpp` — and/or desugared to if; output semantically equivalent (6 tests)
- [x] `test_explicate.cpp` — correct basic block structure with conditional gotos
- [x] `test_select_instructions.cpp` — cmpq/setCC/jCC emitted correctly
- [x] Integration: 6 `.mc` programs in `tests/programs/phase3/`

### 3 — Z3 Predicate Tests ✅

- [x] `well_typed_no_stuck(prog)` — well-typed programs don't get stuck (progress)
- [x] `shrink_equiv(and_expr, if_expr)` — desugared form semantically equivalent

### 3 — Lean Proofs — Partial

- [x] `bool_encoding_roundtrip` — and/or desugaring is equivalent (proven)
- [ ] `type_progress` — well-typed expr is a value or can step (sorry)
- [ ] `type_preservation` — stepping preserves types (sorry)
- [ ] `shrink_preserves_semantics` — desugaring and/or to if preserves eval (sorry)

---

## Phase 4: Loops & Dataflow Analysis (Book Ch. 5, pp. 81-93) ✅

**Language: L_While** — adds `while`, `set!` (mutable variables), `begin`, `void`.

### 4A — Frontend ✅

- [x] New tokens: `WHILE`, `SET`, `BEGIN`, `VOID`
- [x] Grammar: `while (expr) expr`, `set! var expr`, `begin { expr; ... }`, `void`
- [x] AST nodes: `WhileExpr`, `SetBangExpr`, `BeginExpr`, `VoidExpr`, `GetExpr`
- [x] `NodeKind` enum + `kind()` virtual — replaces all `dynamic_cast` dispatch with `switch`/`static_cast`
- [x] Type checker: `set!` type must match var → `Void`; `while` condition `Bool` → `Void`; `begin` → last expr type; `void` → `Void`
- [x] Interpreter: flat mutable env, while loop, begin sequences, void/monostate

### 4B — Compiler Passes ✅

- [x] **uncover_get** — collect set! targets, replace VarExpr → GetExpr for mutable vars
- [x] Update **shrink**, **uniquify**, **rco** — pass through Phase 4 nodes
- [x] **explicate_control** — `while` → loop_entry/loop_body/loop_exit blocks with Goto back-edge; `begin` → chained effects; `set!` → assign stmt; 4 work handlers (tail/pred/assign/effect) split from monolithic process_work
- [x] **Worklist liveness** (Kildall) — `build_cfg()` + fixpoint iteration on cyclic CFG
- [x] No global state — `label_counter` passed as `int&`
- [x] End-to-end: 5 phase4 .mc programs compile correctly; all prior phases still pass

### 4 — Tests ✅

- [x] `test_interpreter.cpp` — while/set!/begin/void (4 Phase 4 tests)
- [x] `test_type_checker.cpp` — while/set!/begin/void + rejections (7 Phase 4 tests)
- [x] `test_explicate.cpp` — while→loop blocks, set!→assign, begin chains (3 Phase 4 tests)
- [x] `test_liveness.cpp` — multi-block Jmp, diamond CFG, loop cycle fixpoint (3 tests)
- [x] `test_rco.cpp` — while/set!/begin/void pass-through (4 tests)
- [x] `test_uniquify.cpp` — while/begin/void preserved (3 tests)
- [x] `test_shrink.cpp` — while/set!/begin/void pass-through (4 tests)
- [x] `test_uncover_get.cpp` — mutable vars identified, VarExpr→GetExpr (4 tests)
- [x] `test_pipeline.cpp` — while loop + nested while integration tests
- [x] 5 `.mc` programs in `tests/programs/phase4/`

### 4 — Z3 Predicate Tests ✅

- [x] `z3_phase4_type_rules` — while (Bool cond required) + set! (type match) via Z3 UNSAT
- [x] `z3_no_var_for_mutable` — uncover_get: no VarExpr remains for set! targets

### 4 — Lean Proofs ✅

- [x] `AST.lean` — Phase 4 nodes: `while_`, `set_`, `begin`, `void_`, `get` + `Ty.void`
- [x] `Shrink.lean` — implemented (pattern-match desugaring), `or_encoding_roundtrip` proven
- [x] `TypeChecker.lean` — `type_check_env` with env, `while_type_void` theorem proven
- [x] `Uniquify.lean` — `uniquify_expr` with counter threading, implemented
- [x] `UncoverGet.lean` — `collect_mutable_vars` + `replace_vars`, implemented
- [x] `ExplicateControl.lean` — Atom/Tail/BasicBlock/CProgram IR types
- [x] `RCO.lean` — `is_atomic` predicate
- [x] Stubs: `AssignHomes.lean`, `PatchInstructions.lean`, `PreludeConclusion.lean`
- [x] 7 sorrys remain (backend passes + RCO tmp generation)

### 4 — Deferred

- [ ] **Challenge: optimize blocks** — remove trivial jumps, merge blocks

---

## Phase 5: Tuples & Garbage Collection (Book Ch. 6, pp. 95-124) ✅

**Language: L_Tup** — adds heap-allocated tuples (fixed-length heterogeneous vectors).

### 5A — Frontend ✅

- [x] New tokens: `Comma`, `LBracket`, `RBracket`, `VectorKw`, `Length`
- [x] Grammar: `vector(e1, ..., en)`, `e[i]` (vector-ref), `e[i] = val` (vector-set!), `length(e)`
- [x] AST nodes: `VectorExpr`, `VectorRefExpr`, `VectorSetExpr`, `VectorLengthExpr`, `AllocateExpr`, `CollectExpr`, `GlobalValueExpr`
- [x] Type system: replaced `enum Type` with parameterized `TypePtr = shared_ptr<Type>`, `TypeKind{Int,Bool,Void,Vector}`, `vector_type(elems)`
- [x] Type checker: vector well-typed, ref returns elem type, set! returns Void, length returns Int, OOB rejected, nested vectors
- [x] Interpreter: `Tuple = shared_ptr<TupleData>` for heap-allocated tuples with aliasing semantics

### 5B — Runtime: Garbage Collector ✅

- [x] Two-space Cheney copying collector in `runtime.c`
  - [x] `initialize(rootstack_size, heap_size)` — calloc from/tospace + rootstack
  - [x] `collect(rootstack_ptr, bytes_requested)` — Cheney BFS copy, swap spaces
  - [x] 64-bit tag: bit 0 = forwarding, bits 1-6 = length, bits 7+ = pointer mask
  - [x] Root stack for spilled tuple-typed vars via R15
  - [x] Globals: `free_ptr`, `fromspace_begin/end`, `tospace_begin/end`, `rootstack_begin`

### 5C — Compiler Passes ✅

- [x] **expose_allocation** (new pass, after shrink/uniquify/uncover_get, before RCO) — lower `vector(e1..en)` to let-bound temps + GC check + `allocate` + `vector-set!` init
- [x] Updated all upstream passes (shrink, uniquify, uncover_get) to handle new AST nodes
- [x] Updated **RCO** — atomize VectorRef/VectorSet/VectorLength operands; Allocate/Collect/GlobalValue as complex
- [x] Updated **explicate_control** — new CExpr types (`CAllocateExpr`, `CVectorRefExpr`, `CVectorSetExpr`, `CVectorLengthExpr`, `CGlobalValueExpr`, `CCollectExpr`); Let handling in assign/pred positions
- [x] Updated **select_instructions** — bump `free_ptr(%rip)` + tag store for allocate; `8*(i+1)(%r11)` offsets for ref/set; `andq`/`sarq` for length; `GlobalArg{name}` → `name(%rip)`; `callq collect` with R15→%rdi
- [x] Updated **liveness** + **interference** — reads/writes for `Andq`/`Sarq`/`Leaq`
- [x] Updated **assign_homes** — tuple-typed spills → `Deref{R15, 8*slot}`, non-tuple → `Deref{Rbp, -8*slot}`
- [x] Updated **patch_instructions** — `GlobalArg` treated as memory; `Andq` patching
- [x] Updated **prelude/conclusion** — `callq initialize`; `movq rootstack_begin(%rip), %r15`; zero root stack slots
- [x] Updated **emit** — emit `GlobalArg`, `Andq`, `Sarq`, `Leaq`
- [x] X86 IR extended: `GlobalArg`, `Andq`, `Sarq`, `Leaq`, `root_stack_space`, `var_types`

### 5 — Tests ✅

- [x] `test_type_checker.cpp` — 7 new tests: vector well-typed, ref type, set! void, length int, OOB reject, type mismatch, nested
- [x] `test_pipeline.cpp` — 3 new integration tests: SimpleTuple, TupleSet, TupleLength
- [x] 5 `.mc` programs in `tests/programs/phase5/` (simple_tuple, tuple_set, tuple_length, nested_tuple, alias)

### 5 — Deferred

- [ ] Z3 predicate tests (`test_gc_z3.cpp` — tag roundtrip, rootstack invariant)
- [ ] Lean stubs (`AST.lean` Ty.vector, `Passes/ExposeAllocation.lean`, `GC.lean`)
- [ ] Per-pass unit tests for expose_allocation, GC tags, GC runtime
- [ ] GC stress integration test

---

## Phase 6: Functions (Book Ch. 7, pp. 125-141) — IN PROGRESS

**Language: L_Fun** — top-level function definitions, first-class function pointers, tail calls.

### 6A — Frontend ✅

- [x] New tokens: `Fn`, `Arrow` (`->`), `Colon` (`:`), `Int_kw`, `Bool_kw`
- [x] Grammar: `fn name(params) : type { body }`, function application `f(args)`, type annotations
- [x] AST nodes: `DefNode`, `ApplyExpr`, `FunRefExpr`; `Program.defs` vector
- [x] Type system: `TypeKind::Function`, `fun_type(params, ret)`, `is_fun_type()`
- [x] Type checker: build env from all defs (mutual recursion); check arg count/types; return type matches body
- [x] Interpreter: `FunctionValue{name, arity}` in Value variant; lookup def + eval body on apply

### 6B — Compiler Passes ✅

- [x] **reveal_functions** (new) — `Var(f)` → `FunRef(f, arity)` for function names
- [x] **limit_functions** (new, stub) — passthrough for ≤6 params (full impl deferred)
- [x] All AST passes (shrink, uniquify, uncover_get, expose_allocation, rco) — handle Apply/FunRef + loop over defs
- [x] C IR extended: `CFunRefExpr`, `CCallExpr`, `CTailCallExpr`; `TailCall` tail; `CFunctionDef`
- [x] x86 IR extended: `IndirectCallq`, `TailJmp`; `X86FunctionDef`
- [x] **explicate_control** — per-def CFGs; Apply in tail position → `TailCall` (function defs only); Apply in non-tail → `CCallExpr`
- [x] **select_instructions** — `FunRef` → `leaq f(%rip)`; `CCallExpr` → args to rdi/rsi/rdx/rcx/r8/r9 + `IndirectCallq`; `TailCall` → args + `TailJmp`; param movs in start block
- [x] **liveness** — `IndirectCallq`/`TailJmp` reads tracked
- [x] **interference** — `IndirectCallq` clobbers caller-saved (like `Callq`)
- [x] **assign_homes** — per-function independent liveness/interference/coloring
- [x] **patch_instructions** — per-function; IndirectCallq/TailJmp pass through
- [x] **prelude/conclusion** — per-function prologue/epilogue; label prefixing to avoid collisions; TailJmp preceded by frame teardown; only main calls `initialize`
- [x] **emit** — function defs emitted with `.globl`; `IndirectCallq` → `callq *arg`; `TailJmp` → `jmp *arg`
- [x] End-to-end: 6 phase6 .mc programs (simple_call, two_args, multi_fn, factorial/sum_to, mutual_recursion, tail_call)

### 6 — Tests — PARTIAL

- [x] All 19 existing unit/integration tests pass (backward compat)
- [x] 6 `.mc` programs in `tests/programs/phase6/` compile + run correctly
- [ ] `test_reveal_functions.cpp` — unit tests for reveal_functions pass
- [ ] `test_limit_functions.cpp` — unit tests for limit_functions pass
- [ ] `test_parser.cpp` — parse def, parse apply, parse type annotations
- [ ] `test_type_checker.cpp` — function def/apply type checking tests
- [ ] `test_interpreter.cpp` — function call/recursion tests
- [ ] `test_select_calls.cpp` — args in correct registers, IndirectCallq/TailJmp
- [ ] `test_pipeline.cpp` — function pipeline integration tests
- [ ] Z3: `test_calling_convention_z3.cpp` — args_in_correct_registers, tail_call_no_stack_growth

### 6 — Known Issues / TODO

- [ ] `limit_functions` — full implementation (pack >6 args into tuple); currently stub
- [ ] Higher-order function tests (pass/return functions) — not yet tested
- [ ] `.align 8` on function labels in emit
- [ ] Lean stubs: `AST.lean` update, `Passes/RevealFunctions.lean`, `Passes/LimitFunctions.lean`
- [ ] Lean theorems: `calling_convention_correct`, `tail_call_stack_invariant`
- [ ] Changeset `.changeset/009_functions.md`
- [ ] Pre-existing bug: `read() + 10` fails compilation (explicate_control assertion in `make_atom` for ReadExpr in non-atomic position — pre-dates Phase 6)

---

## Phase 7: Lexically Scoped Functions / Closures (Book Ch. 8, pp. 143-158)

**Language: L_Lambda** — adds `lambda` (anonymous functions) with lexical scoping.

### 7A — Frontend

- [ ] New tokens: `LAMBDA`, `PROCEDURE_ARITY`
- [ ] Grammar: `lambda (params) -> type { body }`, `procedure_arity(e)`
- [ ] AST nodes: `LambdaExpr`, `ProcArityExpr`
- [ ] Type checker: lambda body checked in env extended with params + enclosing scope
- [ ] Interpreter: closures capture environment

### 7B — Compiler Passes

- [ ] **convert_assignments** — identify vars that are both free in a lambda AND assigned; box them (allocate as 1-element tuple); replace reads/writes with `vector-ref`/`vector-set!`
- [ ] **convert_to_closures** (closure conversion) — lambda -> `Closure(arity, FunRef(name, arity), fv1, fv2, ...)`; create top-level `Def` for each lambda with extra `clos` param; body loads free vars from closure tuple; application -> extract fn ptr from closure[0], call with closure as first arg; `FunRef` -> `Closure(arity, FunRef(f, arity))` (wrap named fns too)
- [ ] Update **expose_allocation** — `Closure` -> `AllocateClosure` (like `Allocate` but stores arity in tag bits 57-61)
- [ ] Update **select_instructions** — `AllocateClosure` like `Allocate` but with arity in tag; `procedure-arity` -> extract bits 57-61 from tag
- [ ] **Challenge: optimize closures** — skip closure wrapping for non-escaping globals; direct calls for known callees
- [ ] Test: closures capturing variables, returned closures, closures over mutable state

### 7 — Tests (TDD)

- [ ] `test_convert_assignments.cpp` — assigned+free vars boxed correctly
- [ ] `test_closure_conversion.cpp` — lambdas lifted to top-level defs, free vars captured
- [ ] `test_free_vars.cpp` — free variable analysis complete and correct
- [ ] Integration: closure programs in `tests/programs/phase7/`

### 7 — Z3 Predicate Tests

- [ ] `free_vars_subset_captured(lambda)` — ForAll fv in free_vars(body), fv in closure tuple
- [ ] `closure_tuple_has_all_fvs(closure)` — closure contains exactly the needed free vars

### 7 — Lean Proofs

- [ ] `free_vars_complete` — free variable analysis finds all free vars
- [ ] `closure_conversion_preserves_semantics` — closure-converted program evaluates same as original

---

## Phase 8: Dynamic Typing (Book Ch. 9, pp. 159-175) — *Optional/Advanced*

**Language: L_Dyn** — dynamically typed subset; values carry runtime type tags.

- [ ] Tagged value representation: 3 low bits encode type (001=int, 100=bool, 010=tuple, 011=procedure, 101=void)
- [ ] `Inject` (tag a value) and `Project` (check tag + extract) operations
- [ ] L_Any intermediate language with `Any` type
- [ ] **cast_insertion** — compile L_Dyn to L_Any by inserting Inject/Project
- [ ] Runtime type predicates: `integer?`, `boolean?`, `vector?`, `procedure?`, `void?`
- [ ] Select instructions: `Inject` -> shift + OR tag; `Project` -> check tag + shift; `trapped-error` -> exit(255)
- [ ] Test: polymorphic programs, type errors at runtime

---

## Phase 9: Gradual Typing (Book Ch. 10, pp. 177-194) — *Optional/Advanced*

**Language: L_?** — mix static and dynamic typing with `Any` type annotation.

- [ ] Cast insertion between static and dynamic types
- [ ] Proxy objects for higher-order casts (function/tuple proxies)
- [ ] `lower_casts`, `differentiate_proxies` passes
- [ ] Test: gradual programs mixing typed and untyped code

---

## Best Practices Audit (see `bestpractices_roadmap.md`)

### Phase 1: Bug Fixes + Quick Wins — DONE
- [x] Global mutable state → local param
- [x] Silent error → `assert(false)`
- [x] Magic numbers → `constexpr` constants

### Phase 2: Safety + Expressiveness — DONE
- [x] 212× unsafe `static_cast` → `expr_cast<T>()` with debug assert
- [x] `parse_file` returns `std::optional<Program>`
- [x] Manual index loops → range-for (2 converted; 19/21 genuinely need index)

### Phase 3: Namespace Wrap — DONE
- [x] All headers → `namespace mc {}`; IR → `mc::cir`, `mc::x86`

### Phase 4: DRY Refactor — PARTIAL
- [x] `clone_leaf()` helper for shared leaf cases across 5 passes
- [ ] Frame structs kept separate (intentional)

### Phase 5: Process
- [x] Structured bindings — already in use
- [ ] CI — deferred

---

## Cross-Cutting Concerns (ongoing)

- [ ] Error reporting with source locations
- [ ] Comprehensive test harness (expected output, type errors, runtime errors)
- [ ] CI: build + test on each commit
- [ ] Grammar spec per phase in BNF
