# mini-compiler

A C++17 compiler from a small ML-flavoured language to x86-64 assembly — graph-colouring register allocation, a copying GC, closures, dynamic typing — built as a testbed for a question: **how much correctness does formal verification actually buy in a compiler, and what do the receipts look like when you audit them?**

[![CI](https://github.com/zakiller34/mini-compiler/actions/workflows/ci.yml/badge.svg)](https://github.com/zakiller34/mini-compiler/actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Lean 4](https://img.shields.io/badge/Lean%204-core%20only-blueviolet)
![Z3](https://img.shields.io/badge/Z3-SMT-green)
![License: MIT](https://img.shields.io/badge/license-MIT-lightgrey)

**TL;DR**

- Eight language phases, a 20-pass nanopass pipeline, source → x86-64 AT&T assembly. Follows Siek's [*Essentials of Compilation*](https://mitpress.mit.edu/9780262047760/) (2023), reimplemented from Racket in C++.
- Five checking regimes: Hoare-style contracts on every function, 316 unit/integration tests, 33 Z3-proved bit-level properties, 33 Lean 4 theorems, and a differential harness running every program through both the interpreter and the compiled binary.
- Writing *honest theorem statements* — before proving anything — exposed **seven real bugs** in the pass models, including an `and` that survived the pass whose whole job is eliminating `and`.
- [`lean/Hygiene.lean`](lean/Hygiene.lean) fails the build on a vacuous theorem, an untracked `sorry`, or `native_decide`: verification of the verification.
- The boundary of what is proved is stated exactly, below. Nothing is overclaimed.

---

## See it run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

cat > answer.mc <<'EOF'
let x = read();
let y = read();
x + -(y)
EOF

./build/src/mc answer.mc -o answer.s
gcc -o answer answer.s build/src/libmc_runtime.a -lm
echo "52 10" | ./answer; echo $?             # 42 — the value is the exit status

echo "52 10" | ./build/src/mc -i answer.mc   # or interpret it, same answer
```

That last line matters more than it looks: the interpreter and the compiled binary share only the lexer, parser and type checker. Everything downstream is independent, so agreement between them is evidence and disagreement localises a fault. More on that below.

---

## The machine

The language grows in eight phases, each adding a feature and the passes to compile it:

| Phase | Language | Adds |
|---|---|---|
| 1 | L_Var | integers, `let`, `read()`, arithmetic |
| 2 | — | graph-colouring register allocation (DSATUR + move biasing) |
| 3 | L_If | booleans, `if`, comparisons, short-circuit `and`/`or` |
| 4 | L_While | `while`, `set!`, `begin`, mutable variables |
| 5 | L_Tup | heap tuples, Cheney two-space copying GC, root stack |
| 6 | L_Fun | top-level functions, System V ABI, tail calls |
| 7 | L_Lambda | lambdas, closure conversion, assignment conversion |
| 8 | L_Dyn / L_Any | dynamic typing, 3-bit runtime tags, trapped errors |

Twenty passes carry a program from source to assembly, plus a free-variable analysis ([`src/passes/free_vars.cpp`](src/passes/free_vars.cpp)) shared by closure conversion:

```
Source (.mc)
  → Lexer → Parser → AST
  → shrink → uniquify → reveal_functions → cast_insert → reveal_casts
  → convert_assignments → convert_to_closures → limit_functions
  → uncover_get → expose_allocation → remove_complex_operands
  → explicate_control                                    ── C_Var IR (basic blocks)
  → select_instructions                                  ── pseudo-x86
  → liveness → interference → graph_coloring → assign_homes
  → patch_instructions → prelude_conclusion → emit       ── x86-64 AT&T assembly
```

Each pass takes an AST or IR by const reference and returns a new one. Nothing mutates in place, which makes every pass independently testable and every intermediate program printable.

| Metric | Value |
|---|---|
| Compiler source | 12,802 lines C++17 |
| Test source | 6,144 lines (316 cases, 32 binaries) |
| Lean development | 1,750 lines (model 1,624 + hygiene checker) |
| C runtime | 164 lines |
| Z3-proved properties | 33 |
| Lean theorems | 33 — 30 proved, 3 stated with tracked `sorry` |
| End-to-end programs | 50, each run through both execution paths |

Parts that are more than exercises:

- **Register allocation** is DSATUR graph colouring over an interference graph built from backward liveness analysis, with move biasing (prefer giving the source and destination of a `movq` the same colour, so the move can be deleted). Liveness is a Kildall worklist fixpoint, because `while` makes the control-flow graph cyclic.
- **The collector** is a Cheney two-space copier. Tuple-typed variables that spill cannot go on the ordinary stack — the collector would not find them — so they spill to a separate *root stack* addressed through `%r15`. Every heap object carries a 64-bit header: bit 0 is a forwarding flag, bits 1–6 the length, bits 7+ a pointer mask saying which slots to trace.
- **Closure conversion** turns a lambda into a heap tuple of its free variables plus a code pointer, and lifts the body to a top-level function taking that tuple as an extra first parameter. Variables that are both assigned and captured are boxed first, so mutation is shared rather than copied.
- **Dynamic typing** steals the low three bits of every value for a type tag: `001` integer, `010` tuple, `011` procedure, `100` boolean, `101` void. `000` is deliberately unused, so the collector can distinguish a tagged value from a plain tuple pointer. Dynamic integers are consequently 61-bit.

---

## What is and is not proved

Every claim in this document should be read against this table, so it comes first.

| Property | Status |
|---|---|
| `shrink` eliminates every `and`/`or` | **proved** |
| `reveal_casts` leaves no `inject`/`project`/predicate | **proved** |
| `reveal_functions` leaves no `var` naming a function | **proved** |
| `uncover_get` leaves no `var` naming a mutable variable | **proved** |
| Free variables of a lambda are all captured | **proved** |
| A variable is boxed iff assigned *and* captured | **proved** |
| `let` and lambda parameters bind (not free in the result) | **proved** |
| Runtime tags separate the five shapes | **proved** |
| The coercion types cast insertion introduces are flat | **proved** (as two point facts, not a universal — the file says so) |
| A failed projection reaches `Exit` (syntactically) | **proved** |
| Liveness, interference and colouring soundness (on models) | **proved** |
| A-normal form after RCO | *stated, tracked `sorry`* |
| No shadowing after `uniquify` | *stated, tracked `sorry`* |
| Uniquify's counter is monotone | *stated, tracked `sorry`* |
| **Semantics preservation, for any pass** | **not modelled** |
| **Type progress and preservation** | **not modelled** in Lean¹ |
| **The entire backend** (explicate, select, assign homes, patch, prelude) | **not modelled** |

¹ A Z3 test checks a finite bit-vector encoding of progress; that is a sampled property of the encoding, not the theorem.

Two qualifications about what "proved" means here.

**These are syntactic properties of a Lean model, not of the C++.** The model is a hand-written mirror of the passes; nothing checks that it matches the C++, and where the two have diverged — `revealCasts` hard-codes a temporary name where the C++ generates fresh ones — the file says so. Proving things about the model catches design errors, not transcription errors. Transcription is what the 316 tests and the differential harness are for.

**Semantics preservation is absent, and that is the interesting boundary.** `shrink_preserves_semantics` cannot even be *stated* without an operational semantics, and for this language that is not a small addition. The signature people reach for, `eval : Env → Expr → Option Value`, is insufficient in four separate ways: `set!` and `vectorSet` mutate, so it needs a store; `read` is I/O, so it needs an input stream; `while` and general application need not terminate, so Lean demands a fuel parameter; and a trapped dynamic type error is a third outcome, distinct from both "value" and "stuck". That is roughly 400 lines and several days before a single theorem, plus a fuel-monotonicity lemma every subsequent proof depends on — and each preservation theorem is days to weeks on its own. Verification cost is superlinear in language features; the syntactic tier is the affordable one, and this project stopped there deliberately.

---

## Where formal verification paid

### Writing the honest statement found seven real bugs

The highest-yield moment of the whole project involved no proof at all. It was writing the *proposition*.

Here is the Lean model of `shrink`, the pass that desugars `and`/`or` into `if`, as it stood:

```lean4
  | .closure a es => .closure a (es.map shrink)
  -- Phase 8 (L_Any) nodes pass through unchanged
  | e => e
```

That catch-all is wrong. The Phase 8 nodes are not leaves — `inject`, `project`, `anyVectorRef` and six others all carry sub-expressions. So `shrink` walked into a program, hit an `inject`, and stopped: an `and` underneath it was never desugared. The moment I wrote the real statement — *every* `and`/`or` is eliminated — the proof refused to go through, correctly, because the proposition was false of the code.

Seven analyses had the same defect. `uniquify` did not rename inside those nodes; `freeVars` under-approximated the free-variable set, which would silently drop closure captures; `collect_mutable_vars` had no `lambda` case at all, so a `set!` inside a lambda body was invisible; `revealCasts` did not recurse *anywhere*. Review had not caught any of it, because every one of these ended in a catch-all that reads as idiomatic completeness rather than as nine skipped cases.

The fixed theorem, in [`lean/MiniCompiler/Passes/Shrink.lean`](lean/MiniCompiler/Passes/Shrink.lean):

```lean4
/-- Shrink eliminates every `and`/`or` node. -/
theorem shrink_no_and_or (e : Expr) : noAndOr (shrink e) = true := by
  induction e using Expr.rec
    (motive_2 := fun es => noAndOrL (es.map shrink) = true) with
  | binary op l r ihl ihr => cases op <;> simp [shrink, noAndOr, ihl, ihr]
  | nil => simp [noAndOrL]
  | cons e es ih ihs => simp [noAndOrL, ih, ihs]
  | _ => simp_all [shrink, noAndOr]
```

And the receipt that it is falsifiable:

```lean4
#eval noAndOr (.inject (.binary .or_ (.bool true) (.bool false)) .bool)
-- false        ← the predicate can fail, so the theorem is not vacuous
#eval noAndOr (shrink (.inject (.binary .or_ (.bool true) (.bool false)) .bool))
-- true         ← and it holds only because the congruence cases now exist
#print axioms shrink_no_and_or
-- 'shrink_no_and_or' depends on axioms: [propext, Quot.sound]
```

Before the fix, the second `#eval` returned `false`. The lesson is the one worth hiring for: **specification is where the value is; the proof is the enforcement mechanism.** A theorem prover's real contribution is that it refuses to let a vague statement through.

### One bit, all 2⁶⁴ headers

Some properties are about *all* bit patterns, and a unit test can only sample. The runtime tag encodings are exactly that shape, so they are proved with the SMT solver over 64-bit bit-vectors ([`tests/z3/test_tagging_z3.cpp`](tests/z3/test_tagging_z3.cpp)):

```cpp
/// @brief Scalars survive the tag round-trip over the representable range
/// ∀ v ∈ [-2^60, 2^60): ((v << 3) | 001) >>arith 3 == v
TEST(TaggingZ3, ScalarTagRoundTrip) {
    Ctx c;
    Z3_ast v = c.var("v");
    c.assert_ast(Z3_mk_bvsle(c.ctx, c.bv(-(1LL << 60)), v));
    c.assert_ast(Z3_mk_bvslt(c.ctx, v, c.bv(1LL << 60)));

    Z3_ast tagged = Z3_mk_bvor(c.ctx,
        Z3_mk_bvshl(c.ctx, v, c.bv(kTagShift)), c.bv(kTagInt));
    Z3_ast back = Z3_mk_bvashr(c.ctx, tagged, c.bv(kTagShift));
    c.assert_ast(Z3_mk_not(c.ctx, Z3_mk_eq(c.ctx, back, v)));
    EXPECT_TRUE(c.unsat());       // negation unsatisfiable ⇒ property holds
}
```

Thirty-three such properties: valid graph colourings, liveness soundness, calling-convention register assignment, the closure-capture invariant, and the GC header invariants — including the one that justifies a design decision. The object length starts at bit 1, not bit 0, because bit 0 is the forwarding flag. Had the length started at bit 0, **every live object of odd length would read as already-forwarded**, and the collector would follow its length field as an address: silent heap corruption, unreachable by fuzzing, invisible to AddressSanitizer because the writes come from generated assembly. That is a single-bit design constraint, it is true of all 2⁶⁴ headers, and no sampling test can even state it.

### Verifying the verification

The most differentiated artifact in the repository is [`lean/Hygiene.lean`](lean/Hygiene.lean), which inspects every elaborated declaration in the project and fails the build on three things:

```lean4
/-- A conclusion with no content: `True`, a disjunction with a `True` side, or
    a reflexive equation. -/
def vacuousBody (b : Lean.Expr) : Bool :=
  b.isConstOf ``True
  || (b.isAppOfArity ``Or 2 && (hasTrue b.appFn!.appArg! || hasTrue b.appArg!))
  || (b.isAppOfArity ``Eq 3 && b.appFn!.appArg! == b.appArg!)
```

- a theorem whose conclusion, after stripping binders, is vacuous;
- any declaration depending on `sorryAx` that is not in a checked-in allowlist **of names** — a count would let a fixed `sorry` be silently replaced by a new one elsewhere;
- any declaration depending on `Lean.ofReduceBool`, i.e. `native_decide`. That tactic closes goals the kernel cannot check; it is exactly the tool you reach for when a proof is stuck, and exactly the wrong trade for a repository whose selling point is rigor.

Two things it took a real run to learn. Filter by **module**, not by namespace: `AST.lean` opens no namespace and several pass files declare theorems at the root, so a `MiniCompiler.`-prefix test silently skips eleven theorems — the exact blind spot the checker exists to prevent. And gate on `!nm.isInternalDetail`, or you scan several hundred compiler-generated equation lemmas instead of the real declarations.

It is tested the only way a guard should be — by reintroducing the bug:

```
$ lake build
error: Hygiene.lean:73:0: PROOF HYGIENE FAILURE
  vacuous conclusions (1): [MiniCompiler.sneaky_regression]
```

### The false-statement taxonomy

Not every weak theorem can be made honest by writing the statement. `limit_functions` rewrites functions with more than six parameters to pack the excess into a tuple; the C++ implements it in 371 lines, but the Lean model is `fun p => p` — the identity. So the natural statement,

> `∀ d ∈ (limit_functions p).defs, d.params.length ≤ 6`

is **false** of the model. Writing it with a `sorry` would leave the repository formally asserting something untrue, and anyone who later discharged that `sorry` would have derived `False`. There is a ranking here, worth stating plainly:

1. Real statement, real proof.
2. Real, *true* statement with a tracked `sorry` — honest debt.
3. Vacuous statement — proves nothing, harms nothing except your credibility.
4. **False statement with a `sorry`** — actively poisonous.

Two theorems sat at (4) as their only honest option, and were deleted, with the reason recorded in the file ([`lean/MiniCompiler/Passes/LimitFunctions.lean`](lean/MiniCompiler/Passes/LimitFunctions.lean)). A vacuous theorem is useless; a false one is poison.

---

## Where it didn't pay — and what covered the gap

The credibility of the section above rests on being equally precise about the failures. There were three, and each was caught by a *different* regime — which is the actual argument for running several.

### Twenty theorems that proved nothing

An audit found that twenty of the then-thirty-five Lean theorems were this, while typechecking green:

```lean4
/-- Closure conversion preserves evaluation semantics. -/
theorem closure_conversion_preserves_semantics : ∀ _p : Program, True := by
  intro _; trivial
```

Valid Lean. Green build. Roadmap ticked. Two more were worse because they looked real — a conclusion of the form `… ∨ True`, unfalsifiable by construction.

It survives review because **every signal a reviewer uses is intact**: right file, honest-sounding name, correct docstring, short proof that reads as elegant rather than empty. The only wrong thing is the proposition between the colon and the `:=`, which is the one place a reader's eye skips. This is not a Lean quirk; it is the general failure mode of verification as a practice. A proof is a certificate that a *statement* holds — if nobody checks the statement, the certificate is decoration. The same shape appears as a test asserting `expect(x).toBeDefined()` or an alert on a metric nobody emits.

This is why the fix was not "fix the theorems" (one commit, done) but `Hygiene.lean` (a build gate, permanent). The twenty vacuous theorems are best understood as the control experiment: what verification looks like when nobody checks the statements — and the reason meta-verification is now part of the build.

In the same family: `RevealFunctions.lean` **had not compiled since Phase 8 landed**. Nobody saw it, because the lakefile had no `globs` and nothing imported it — Lake builds the root module and whatever it transitively imports, and two modules were simply never elaborated. The fix is structural: `globs := #[.andSubmodules `MiniCompiler]` in [`lean/lakefile.lean`](lean/lakefile.lean), plus a pre-commit assertion that every module under `MiniCompiler/` is imported by the root. A green check mark is a claim about whatever was actually checked, and the set of things actually checked is itself worth checking.

### `add(20, 22) == 40`

One live miscompilation, in territory the table above marks **not modelled** — no proof discipline could have caught it, because the Lean model stops before the backend. This is the case for the cheapest regime in the project.

```
fn add(x: Int, y: Int) : Int { x + y }
add(20, 22)
```

returned 40, and had since Phase 6, through two subsequent language phases and what was then a 287-case suite (316 today). The function's entry sequence as emitted:

```
add_start:
    movq %rdi, %rcx     ; closure pointer → its home
    movq %rsi, %rdx     ; x → its home, %rdx
    movq %rdx, %rcx     ; y ← %rdx ... which was just overwritten with x
    movq %rdx, %rax
    addq %rcx, %rax     ; x + x
```

The System V ABI passes arguments in `%rdi, %rsi, %rdx, ...`, and the entry sequence is a **parallel move** emitted as a sequence. `x`'s home is `%rdx`, which still holds the incoming `y`. Root cause: liveness tracks *variables*, and physical registers are invisible to it — so the interference graph had no edge, and the allocator was free to clobber. The fix precolours parameters against argument registers still holding later parameters ([`src/passes/assign_homes.cpp`](src/passes/assign_homes.cpp)):

```cpp
if (params != nullptr) {
    const auto &aregs = arg_regs();
    for (size_t i = 0; i < params->size() && i < aregs.size(); ++i) {
        for (size_t j = i + 1; j < params->size() && j < aregs.size(); ++j) {
            graph.add_edge(Location{(*params)[i]}, Location{aregs[j]});
        }
    }
}
```

The caller side has no such hazard, for a checkable reason: `IndirectCallq` is modelled as clobbering all caller-saved registers, so anything live across a call already interferes with every argument register. Confirmed by fuzzing 1,600 random multi-argument programs against the interpreter.

Why did it survive? The function tests asserted *structure* — a `callq` emitted, a tail call becoming `jmp` — and single-argument functions cannot exhibit the bug. What found it was [`tests/run_differential.sh`](tests/run_differential.sh): 77 lines of shell that run all 50 programs through both the interpreter and the compiled binary and require agreement. It found the bug on its first run, because it asks the only question structural assertions cannot: does the compiled program compute the same value? The regression test now asserts the invariant itself, not an example ([`tests/integration/test_pipeline.cpp`](tests/integration/test_pipeline.cpp)):

```cpp
// Within the entry mov run, no instruction reads a register that an earlier
// instruction in the same run has already written.
std::set<std::string> written;
for (const auto &[src, dst] : movs) {
    EXPECT_EQ(written.count(src), 0u)
        << "entry sequence reads %" << src << " after writing it";
    written.insert(dst);
}
```

— verified by disabling the fix and watching it fail. A regression test never observed failing is itself an unfalsifiable claim.

### The allocator that checked for room too late

The lightest regime — contracts and the discipline of testing stated properties — caught the scariest bug. Phase 5's collector shipped with no direct tests; writing them found the collector fine and its *caller* wrong: `expose_allocation` emitted the heap-exhaustion check *after* bumping `free_ptr`. Two consequences: an object could be placed past `fromspace_end`, and if the check did fire, `collect` ran before the fresh object was rooted — so it was not copied, and `free_ptr` was reset back over it.

It never manifested. Allocator slack absorbed the overflow; shrinking the heap to 48 bytes still produced right answers; AddressSanitizer cannot see writes from generated assembly. A latent memory-corruption bug that testing could not provoke — found because writing a test for a stated property forced me to check whether the property held. The check now precedes the allocation ([`src/passes/expose_allocation.cpp`](src/passes/expose_allocation.cpp)), and ten collector tests cover relocation, reclamation, transitive tracing, shared children, cycles, integers that look like addresses, and stability under repeated collection.

### Who caught what

| Defect | Contracts | Tests | Z3 | Lean | Differential |
|---|---|---|---|---|---|
| 7 model analyses treating `L_Any` nodes as leaves | – | out of scope (model) | – | **found** | – |
| GC header: odd length reads as forwarded | – | cannot state | **proved impossible** | – | – |
| 20 vacuous theorems | – | – | – | **Hygiene.lean** | – |
| `RevealFunctions.lean` unbuilt since Phase 8 | – | – | – | **found** (globs + import check) | – |
| Parameter clobber, `add(20,22)==40` | fix's postcondition | missed | out of scope | not modelled | **found** |
| Heap check emitted after allocation | **found** (writing the test for the stated property) | latent | – | not modelled | – |

Five regimes, six defect classes, and no regime dominates. Each one caught defects only in its own region — which is what defense in depth is supposed to look like, and the honest version of "formal verification adds value": not that proofs replace tests, but that each regime states a kind of claim the others structurally cannot.

---

## Cost, boundary, and what's next

The proved/not-proved table is short because verification was priced deliberately. The syntactic tier — "this pass eliminates that construct", binding structure, tag disjointness — costs hours per theorem and caught seven real bugs. The semantic tier starts at ~400 lines of operational semantics (store, input stream, fuel, trapped errors) before the first theorem, and `uniquify`'s preservation proof needs a renaming bisimulation on top. Phase 9 of the roadmap — gradual typing with proxy objects for higher-order casts — was **declined** for the same reason: the remaining effort was worth more spent making eight phases true than making nine phases green. [ROADMAP.md](ROADMAP.md) records what was declined and why.

Next, in order of value: an operational semantics for the frontend (unlocks the preservation theorems), discharging the three tracked `sorry`s, and a BNF grammar spec per phase.

---

## Building it

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
ctest --test-dir build --output-on-failure   # 32 binaries, 316 cases (24 binaries without libz3-dev)
./tests/run_differential.sh                  # 50 programs, both execution paths

cd lean && lake build                        # proofs + the hygiene checker
./scripts/check_honesty.sh                   # fast pre-commit gate
```

Toolchain: C++17 / CMake / GoogleTest / Z3 C API / Lean 4 (core only — no Mathlib) / clang-tidy on every build / GitHub Actions. CI runs the cold build — where clang-tidy's function-size gate actually fires, since incremental builds skip untouched files — plus the tests, the differential harness, and `lake build` including the proof-hygiene checker.

---

## Reference

Siek, J. G. (2023). *Essentials of Compilation: An Incremental Approach in Racket*. MIT Press. ISBN 9780262047760.

See [ROADMAP.md](ROADMAP.md) for the per-phase plan, including what was declined and why.
