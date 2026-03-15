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

## Phase 3: Booleans & Conditionals (Book Ch. 4, pp. 55-79) — IN PROGRESS

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

### 3 — Tests (TDD) — REMAINING

- [ ] `test_type_checker.cpp` — accept well-typed, reject ill-typed programs
- [ ] `test_shrink.cpp` — and/or desugared to if; output semantically equivalent
- [ ] `test_explicate_if.cpp` — correct basic block structure with conditional gotos
- [ ] `test_select_cmp.cpp` — cmpq/setCC/jCC emitted correctly
- [ ] Integration: 10+ `.mc` programs in `tests/programs/phase3/`

### 3 — Z3 Predicate Tests

- [ ] `well_typed_no_stuck(prog)` — well-typed programs don't get stuck (progress)
- [ ] `shrink_equiv(and_expr, if_expr)` — desugared form semantically equivalent

### 3 — Lean Proofs

- [ ] `type_progress` — well-typed expr is a value or can step
- [ ] `type_preservation` — stepping preserves types
- [ ] `shrink_preserves_semantics` — desugaring and/or to if preserves eval
- [ ] `bool_encoding_roundtrip` — 0/1 encoding of booleans is faithful

---

## Phase 4: Loops & Dataflow Analysis (Book Ch. 5, pp. 81-93)

**Language: L_While** — adds `while`, `set!` (mutable variables), `begin`, `void`.

### 4A — Frontend

- [ ] New tokens: `WHILE`, `SET` (or `=` assignment), `BEGIN`, `VOID`
- [ ] Grammar: `while (expr) expr`, `var = expr` (assignment), `begin { expr; ... }`, `void`
- [ ] AST nodes: `WhileExpr`, `SetBangExpr`, `BeginExpr`, `VoidExpr`
- [ ] Type checker: `set!` type must match var; `while` condition is `Boolean`, result is `Void`; `begin` result is last expr
- [ ] Interpreter: mutable bindings (boxed values), while loop

### 4B — Compiler Passes

- [ ] **uncover_get** — identify mutable variables (appear on LHS of `set!`); replace reads with `get!` to preserve evaluation order under RCO
- [ ] Update **remove_complex_operands** — `get!`, `set!`, `begin`, `while` are complex; subexprs of `set!`/`begin`/`while` may be complex
- [ ] **explicate_control** — `while` becomes loop label + conditional goto; `begin` introduces effect position; CFG now has cycles
- [ ] **Dataflow-based liveness analysis** — worklist algorithm (Kildall); iterate to fixed point on cyclic CFG instead of topological sort
- [ ] Update **select_instructions** — `void` -> `movq $0`; standalone `read` (call without assignment)
- [ ] Test: loop programs (sum 1..n, factorial, fibonacci)

### 4 — Tests (TDD)

- [ ] `test_uncover_get.cpp` — mutable vars correctly identified, reads replaced
- [ ] `test_explicate_while.cpp` — loop label + conditional goto structure correct
- [ ] `test_dataflow_liveness.cpp` — worklist reaches fixed point, correct live sets on cyclic CFG
- [ ] Integration: loop programs in `tests/programs/phase4/`

### 4 — Z3 Predicate Tests

- [ ] `fixed_point_reached(live_sets)` — applying transfer functions again yields same sets
- [ ] `variant_decreases(iteration_state)` — worklist iteration measure strictly decreases

### 4 — Lean Proofs

- [ ] `worklist_terminates` — worklist algorithm terminates (finite lattice, monotone transfer)
- [ ] `dataflow_liveness_sound` — fixed-point liveness is sound (every use covered)

---

## Phase 5: Tuples & Garbage Collection (Book Ch. 6, pp. 95-124)

**Language: L_Tup** — adds heap-allocated tuples (fixed-length heterogeneous vectors).

### 5A — Frontend

- [ ] New tokens: `VECTOR`, `VECTOR_REF`, `VECTOR_SET`, `VECTOR_LENGTH`
- [ ] Grammar: `vector(e1, ..., en)`, `e[i]` (vector-ref), `e[i] = e` (vector-set!), `length(e)`
- [ ] AST nodes: `VectorExpr`, `VectorRefExpr`, `VectorSetExpr`, `VectorLengthExpr`
- [ ] Type checker: `Vector` type parameterized by element types; index must be int literal (statically known); `vector-set!` returns `Void`
- [ ] Interpreter: tuples as heap objects with aliasing semantics

### 5B — Runtime: Garbage Collector

- [ ] Implement two-space copying collector in `runtime.c`
  - [ ] `initialize(rootstack_size, heap_size)` — create FromSpace, ToSpace, root stack
  - [ ] `collect(rootstack_ptr, bytes_requested)` — Cheney's algorithm (BFS copy)
  - [ ] 64-bit tag per tuple: bit 0 = forwarding flag, bits 1-6 = length, bits 7+ = pointer mask
  - [ ] Root stack (shadow stack) for spilled tuple-typed vars
  - [ ] `free_ptr`, `fromspace_begin`, `fromspace_end`, `rootstack_begin` globals

### 5C — Compiler Passes

- [ ] **expose_allocation** — lower `vector(...)` to: sequence of temp bindings, conditional `collect` call, `allocate`, `vector-set!` initialization
- [ ] Update **remove_complex_operands** — `collect`, `allocate`, `global_value` are complex
- [ ] Update **explicate_control** — handle new forms in C_Tup IR
- [ ] **select_instructions** — tuple read/write via `movq` with offset `8(n+1)(%r11)`; `r11` reserved for tuple base; `allocate` -> inline bump `free_ptr`; `collect` -> `callq collect`; `vector-length` -> tag extraction with `andq`/`sarq`; global vars via `label(%rip)` addressing
- [ ] Register allocator: `r11` and `r15` (root stack ptr) removed from allocable set; tuple-typed vars spill to root stack (not regular stack); interference edges between tuple-live vars and callee-saved regs across `collect` calls
- [ ] **prelude/conclusion** — call `initialize`; setup `r15` from `rootstack_begin`; zero root stack slots; adjust `r15` in conclusion
- [ ] Test: tuple creation, nested tuples, aliasing, GC triggering

### 5 — Tests (TDD)

- [ ] `test_expose_alloc.cpp` — vector lowered to allocate + collect + init sequence
- [ ] `test_gc_tags.cpp` — tag encode/decode roundtrip for various lengths and pointer masks
- [ ] `test_gc_runtime.cpp` — Cheney copy preserves reachable objects
- [ ] Integration: tuple programs in `tests/programs/phase5/`, GC stress tests

### 5 — Z3 Predicate Tests

- [ ] `tag_encode_decode_roundtrip(len, mask)` — ForAll len mask, `decode(encode(len,mask)) == (len,mask)`
- [ ] `all_live_tuples_on_rootstack(prog)` — every live tuple-typed var is on root stack across collect calls

### 5 — Lean Proofs

- [ ] `tag_roundtrip` — encode then decode yields original (len, mask)
- [ ] `cheney_preserves_reachable` — all reachable objects survive collection
- [ ] `root_stack_invariant` — root stack always points to valid tuples

---

## Phase 6: Functions (Book Ch. 7, pp. 125-141)

**Language: L_Fun** — top-level function definitions, first-class function pointers, tail calls.

### 6A — Frontend

- [ ] New tokens: `DEF` (or `fn`/`func`), `ARROW` (`->`), `RETURN`
- [ ] Grammar: `def name(params) -> type { body }`, function application `f(args)`, function type `(T1, ...) -> Tr`
- [ ] AST nodes: `DefNode`, `ApplyExpr`, `FunRefExpr`, `ProgramDefs`
- [ ] Type checker: build env from all defs (mutual recursion); check arg types match param types; return type matches body
- [ ] Interpreter: function values, application, mutual recursion

### 6B — Compiler Passes

- [ ] **shrink** — wrap top-level expression in implicit `main() -> int` def
- [ ] **reveal_functions** — distinguish function names from variables: `Var(f)` -> `FunRef(f, arity)`
- [ ] **limit_functions** — pack args 6+ into a tuple as 6th argument
- [ ] Update **remove_complex_operands** — `FunRef` is complex (needs `leaq`); `Apply` is complex
- [ ] **explicate_control** — `Apply` -> `Call` (assignment/predicate context) or `TailCall` (tail context); per-function CFGs
- [ ] **select_instructions** — parameter passing via `rdi, rsi, rdx, rcx, r8, r9`; `FunRef` -> `leaq f(%rip)`; `Call` -> move args to regs + `callq *reg` + `movq %rax, dst`; `TailCall` -> move args + pop frame + `jmp *%rax`; `.align 8` on function labels
- [ ] Liveness: `IndirectCallq` writes all caller-saved regs; `TailJmp` reads arg regs + target
- [ ] Interference: per-function graph; tuple-typed vars interfere with callee-saved regs across any user call (not just `collect`)
- [ ] **allocate_registers** — run per function definition
- [ ] **prelude/conclusion** — per-function prologue/epilogue; `main` prelude calls `initialize` and sets `r15`
- [ ] Test: recursion, mutual recursion, higher-order (pass/return functions), tail calls

### 6 — Tests (TDD)

- [ ] `test_reveal_functions.cpp` — function names replaced with FunRef
- [ ] `test_limit_functions.cpp` — args 6+ packed into tuple
- [ ] `test_select_calls.cpp` — args in correct registers, callq/jmp emitted
- [ ] `test_tail_calls.cpp` — tail calls don't grow stack
- [ ] Integration: function programs in `tests/programs/phase6/`

### 6 — Z3 Predicate Tests

- [ ] `args_in_correct_registers(call_site)` — first 6 args in rdi/rsi/rdx/rcx/r8/r9
- [ ] `tail_call_no_stack_growth(prog)` — tail-recursive calls reuse caller's frame

### 6 — Lean Proofs

- [ ] `calling_convention_correct` — args placed per System V ABI
- [ ] `tail_call_stack_invariant` — stack depth bounded for tail-recursive programs

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

## Cross-Cutting Concerns (ongoing)

- [ ] Error reporting with source locations
- [ ] Comprehensive test harness (expected output, type errors, runtime errors)
- [ ] CI: build + test on each commit
- [ ] Grammar spec per phase in BNF
