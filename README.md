# mini-compiler

*A compiler from a small language to x86-64, written to find out how much correctness you can actually buy — and what the receipts look like when you check them.*

---

This is a C++17 compiler for a small but non-trivial language, targeting x86-64 assembly, built by following Jeremy Siek's [*Essentials of Compilation*](https://mitpress.mit.edu/9780262047760/) (MIT Press, 2023). The book teaches the nanopass method in Racket; this reimplements it in C++, and adds three checking regimes the book does not use: Hoare-style contracts on every function, Z3 predicate tests for the bit-level encodings, and a Lean 4 model of the passes.

The interesting part is not that it works. The interesting part is what happened when the checks were audited.

**Twenty of the thirty-five Lean theorems proved nothing.** Not "were incomplete" — proved nothing, while typechecking green:

```lean4
theorem closure_conversion_preserves_semantics : ∀ _p : Program, True := by
  intro _; trivial
```

And separately, `add(20, 22)` returned **40**. That miscompilation had been live since Phase 6, through two subsequent language phases and a 287-case test suite.

Both are fixed. This document is about how they survived, what found them, and what the fix looks like when the goal is that the same class of failure cannot recur silently.

---

## The machine

The source language grows in eight phases, each adding a feature and the passes to compile it:

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

Twenty-one passes carry a program from source to assembly:

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

Some things this involved that are more than exercises:

- **Register allocation** is DSATUR graph colouring over an interference graph built from backward liveness analysis, with move biasing (prefer giving the source and destination of a `movq` the same colour, so the move can be deleted). Liveness is a Kildall worklist fixpoint, because `while` makes the control-flow graph cyclic.
- **The collector** is a Cheney two-space copier. Tuple-typed variables that spill cannot go on the ordinary stack — the collector would not find them — so they spill to a separate *root stack* addressed through `%r15`. Every heap object carries a 64-bit header: bit 0 is a forwarding flag, bits 1–6 the length, bits 7+ a pointer mask saying which slots to trace.
- **Closure conversion** turns a lambda into a heap tuple of its free variables plus a code pointer, and lifts the body to a top-level function taking that tuple as an extra first parameter. Variables that are both assigned and captured are boxed first, so mutation is shared rather than copied.
- **Dynamic typing** steals the low three bits of every value for a type tag: `001` integer, `010` tuple, `011` procedure, `100` boolean, `101` void. `000` is deliberately unused, so the collector can distinguish a tagged value from a plain tuple pointer. Dynamic integers are consequently 61-bit.

| | |
|---|---|
| Compiler source | 12,802 lines C++ |
| Test source | 6,144 lines C++ |
| Lean model | 1,750 lines |
| C runtime | 164 lines |
| Passes | 21 |
| Test binaries / cases | 32 / 344 |
| End-to-end programs | 50 |
| Lean theorems | 33 (30 proved, 3 stated with tracked `sorry`) |

---

## Four ways of being wrong

The project uses four checking regimes. The claim worth defending is not that four is better than one — it is that they fail in **different directions**, and each one caught defects the others structurally could not.

### Contracts

Every function carries a Lean-style contract as a docstring, and every loop an invariant and a decreases clause:

```cpp
/// @brief Replace VarArg with a register or stack Deref(-N(%rbp))
/// @requires prog has pseudo-x86 with VarArg references
/// @ensures no VarArg remains, stack_space set and 16-aligned
/// @ensures if params is non-null, params[i] is never assigned an argument
///          register that still holds one of params[i+1..]
x86::X86Program assign_homes(const x86::X86Program &prog,
                             const std::vector<std::string> *params = nullptr);
```

These are not checked mechanically. Their value is that writing `@ensures` forces you to name the postcondition, and a postcondition you cannot state is usually one you have not thought about. The second `@ensures` above did not exist until the miscompilation described below forced someone to articulate it.

### Unit and integration tests

344 cases across 32 binaries: per-pass unit tests, pipeline tests from AST to assembly text, and 50 `.mc` programs. This is the regime everyone has, and its blind spot is the one everyone has too — it tests the properties you thought of.

### Z3 predicate tests

Some properties are about *all* bit patterns, and a unit test can only sample. The tag encodings are exactly that shape, so they are proved with the SMT solver over 64-bit bit-vectors:

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

Thirty-three such properties: valid graph colourings, liveness soundness, calling-convention register assignment, the closure-capture invariant, and the GC header invariants — including the one that explains a design decision. The object length starts at bit 1, not bit 0, because bit 0 is the forwarding flag. Had the length started at bit 0, every live object of odd length would read as already-forwarded and the collector would follow its length field as an address. That is a single-bit design constraint, it is true of all $2^{64}$ headers, and no sampling test states it.

### The Lean model

`lean/` mirrors the AST as an inductive type and several passes as total functions over it, with theorems about their output. This is where the audit went badly, and it gets its own section.

### The one that actually earned its place

The regime that found the most is the cheapest: **run every program through both execution paths and require them to agree.**

The interpreter and the compiled binary share only the lexer, parser and type checker. Everything after that — twenty-one passes, register allocation, the collector, the ABI — is independent. So agreement across the corpus is strong evidence, and disagreement localises the fault immediately.

```bash
./tests/run_differential.sh        # 50 programs, interpreter vs compiled exit status
```

It is under eighty lines of shell. It found a live miscompilation that 287 test cases, a suite of SMT properties and a Lean development had all missed.

---

## Twenty theorems that proved nothing

Here is what was in the repository, in a file named `ConvertToClosures.lean`, under a heading about closure conversion:

```lean4
/-- Closure conversion preserves evaluation semantics. -/
theorem closure_conversion_preserves_semantics : ∀ _p : Program, True := by
  intro _; trivial

/-- Every free variable of a lambda body ends up in its closure tuple. -/
theorem free_vars_subset_captured : ∀ _p : Program, True := by
  intro _; trivial
```

Both are valid Lean. Both compile. `lake build` was green. The roadmap had them ticked off. Twenty of thirty-five theorems were this, and two more were worse, because they looked like real theorems:

```lean4
/-- The tag code of a flat type identifies it up to its shape. -/
theorem tagof_injective_on_flat :
    ∀ t₁ t₂ : Ty, isFlat t₁ → isFlat t₂ → tagof t₁ = tagof t₂ →
      (t₁ = .int ∧ t₂ = .int) ∨ True := by
  intro _ _ _ _ _; exact Or.inr trivial
```

The conclusion is a disjunction with `True` on one side. It is unfalsifiable. And the name is wrong on top of that: `tagof` is *not* injective on flat types, since `Vector Any` and `Vector Any Any` share the tag `010`.

### How this survives review

It survives because **every signal a reviewer normally uses is intact**. The file is in the right place. The name states a real property. The docstring paraphrases it correctly. The proof term is short, which reads as elegant rather than empty. The build is green, and the count goes up. The only thing wrong is the proposition between the colon and the `:=`, which is the one place a reader's eye skips, because in every honest proof it is the boring part.

This is the general failure mode of verification as a practice, not a Lean quirk. A proof is a certificate that a *statement* holds. If nobody checks the statement, the certificate is decoration. The same shape appears as a test asserting `expect(x).toBeDefined()`, a type signature returning `any`, a monitor alerting on a metric nobody emits.

### The bug the placeholders were hiding

The reason this matters beyond hygiene: writing the honest statement did not merely produce a `sorry`. It **failed**.

Here is `shrink`, which desugars `and`/`or` into `if`. Its Lean model ended like this:

```lean4
  | .closure a es => .closure a (es.map shrink)
  -- Phase 8 (L_Any) nodes pass through unchanged
  | e => e
```

That catch-all is wrong. The Phase 8 nodes are not leaves — `inject`, `project`, `anyVectorRef` and six others all carry sub-expressions. So `shrink` walked into a program, hit an `inject`, and stopped. An `and` underneath it was never desugared.

Nothing detected this, because the theorem about `shrink` was `True`. The moment it was replaced with the real proposition, the proof would not go through — correctly, because the proposition was false of the code. Seven analyses had the same defect: `uniquify` did not rename inside those nodes, `freeVars` under-approximated the free-variable set (which would silently drop closure captures), `collect_mutable_vars` had no `lambda` case at all, and `revealCasts` did not recurse *anywhere*.

The honest statement, and the check that it is falsifiable:

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

```lean4
#eval noAndOr (.inject (.binary .or_ (.bool true) (.bool false)) .bool)
-- false        ← the predicate can fail, so the theorem is not vacuous
#eval noAndOr (shrink (.inject (.binary .or_ (.bool true) (.bool false)) .bool))
-- true         ← and it holds only because the congruence cases now exist
#print axioms shrink_no_and_or
-- 'shrink_no_and_or' depends on axioms: [propext, Quot.sound]
```

Line one is the whole argument. Before the fix, that expression evaluated to `false` *after* `shrink`, so the theorem was false. The placeholder had been standing in front of a real bug.

### Two theorems deleted rather than repaired

Not everything could be made honest by writing the statement. `limit_functions` rewrites functions with more than six parameters to pack the excess into a tuple. The C++ implements it in 371 lines. The Lean model is `fun p => p` — the identity. So the natural statement,

> `∀ d ∈ (limit_functions p).defs, d.params.length ≤ 6`

is **false** of the model. Writing it with a `sorry` would leave the repository formally asserting something untrue, and anyone who later discharged that `sorry` would have derived `False`. That is strictly worse than a vacuous theorem: a vacuous theorem is useless, a false one is poison.

It was deleted, with the reason recorded in the file. Same for `explicate_has_start`, a theorem about a definition that was itself `sorry` — no theorem about a `sorry`d definition can carry meaning.

There is a ranking here, and it is worth stating plainly:

1. Real statement, real proof.
2. Real, *true* statement with a tracked `sorry` — honest debt.
3. Vacuous statement — proves nothing, but harms nothing except your credibility.
4. **False statement with a `sorry`** — actively poisonous.

Most of the twenty went to (1). Three went to (2). Two went to deletion, because (4) was the only alternative.

### Making it a build failure

Fixing twenty theorems is a one-time act. The check is the deliverable. `lean/Hygiene.lean` inspects every elaborated declaration in the project and fails the build on three things:

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
- any declaration depending on `Lean.ofReduceBool`, i.e. `native_decide`. That tactic will close goals the kernel cannot check. It is exactly the tool you reach for when a proof is stuck, and it is exactly the wrong trade for a repository whose selling point is rigor.

Two things it took a real run to learn. Filter by **module**, not by namespace: `AST.lean` opens no namespace and several pass files declare theorems at the root, so a `MiniCompiler.`-prefix test silently skips nine theorems — the exact blind spot the checker exists to prevent. And gate on `!nm.isInternalDetail`, or you scan several hundred compiler-generated equation lemmas instead of the real declarations.

It is tested the only way a guard should be — by reintroducing the bug:

```
$ lake build
error: Hygiene.lean:73:0: PROOF HYGIENE FAILURE
  vacuous conclusions (1): [MiniCompiler.sneaky_regression]
```

### The green build that was not building the code

One more finding, in the same family. `RevealFunctions.lean` **had not compiled since Phase 8 landed** — `reveal_expr` had no catch-all and predated the new constructors, so it failed with a missing-cases error.

Nobody saw it, because `MiniCompiler.lean` never imported it and the lakefile had no `globs`. Lake builds the root module and whatever it transitively imports; two modules were simply never elaborated. The build was green because the broken file was not in it.

The fix is structural rather than a one-off:

```lean4
@[default_target]
lean_lib «MiniCompiler» where
  srcDir := "."
  globs := #[.andSubmodules `MiniCompiler]   -- every file, imported or not
```

And a line in the pre-commit script asserting that every module under `MiniCompiler/` is imported by the root. A green check mark is a claim about whatever was actually checked, and the set of things actually checked is itself worth checking.

While auditing this, one more documented claim turned out to be false: `CLAUDE.md` advertised local `mathlib4 + cslib` dependencies. `lakefile.lean` has no `require`, `lake-manifest.json` reads `"packages": []`, and no file imports Mathlib. The development uses core Lean only — which is a better story than the one that was written down, and has the advantage of being true.

---

## `add(20, 22) == 40`

The other finding is a plain miscompilation, and it is instructive for the opposite reason: no amount of proof discipline would have caught it, because the Lean model does not cover the backend at all.

```
fn add(x: Int, y: Int) : Int { x + y }
add(20, 22)
```

returned 40. Here is the function's entry sequence as emitted:

```
add_start:
    movq %rdi, %rcx     ; closure pointer → its home
    movq %rsi, %rdx     ; x → its home, %rdx
    movq %rdx, %rcx     ; y ← %rdx ... which was just overwritten with x
    movq %rdx, %rax
    addq %rcx, %rax     ; x + x
```

The System V ABI passes arguments in `%rdi, %rsi, %rdx, ...`, and the register allocator assigns each parameter a home. The entry sequence is therefore a **parallel move** — all the assignments are meant to happen simultaneously — but it is emitted as a sequence. Here `x`'s home is `%rdx`, which is still `y`'s incoming argument register. Writing `x` destroys `y` before it is read.

The root cause is one line in liveness analysis:

```cpp
void var_from_arg(const x86::Arg &a, std::set<std::string> &out) {
    if (const auto *v = std::get_if<x86::VarArg>(&a)) {
        out.insert(v->name);
    }
}
```

Liveness tracks *variables*. Physical registers are invisible to it. So nothing recorded that `%rdx` held a live value at that point, the interference graph had no edge, and the allocator was free to put `x` there.

The fix precolours: parameter `i` gets an interference edge against every argument register still holding a later parameter, so it can never be homed there.

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

The obvious follow-up question is whether the *caller* has the same hazard — it also fills argument registers in sequence. It does not, and for a reason worth knowing: `IndirectCallq` is modelled as clobbering all caller-saved registers, so any variable live across a call already interferes with every argument register. The callee's entry had no equivalent mechanism, which is precisely why only that side broke. Confirmed by fuzzing 1,600 random multi-argument programs against the interpreter.

### Why it survived so long

Because the tests asserted the wrong things. There were tests for functions — `SimpleFnCall`, `RecursiveFn`, `MutualRecursion`, `TailCall` — and they passed. They checked structure: that a `callq` was emitted, that arguments went to argument registers, that a tail call became a `jmp`. None of them checked *what the program computed*, and single-argument functions cannot exhibit the bug at all.

The differential harness found it in the first run, because it asks the only question that cannot be answered structurally: does the compiled program produce the same value as the interpreter?

The regression test now asserts the actual invariant rather than an example:

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

Verified by disabling the fix and watching it fail — a regression test never observed failing is itself an unfalsifiable claim.

---

## The collector had no tests

Phase 5 shipped a Cheney copying collector — the component where a bug corrupts memory rather than producing a wrong number — with no direct tests. It was exercised only end-to-end through generated assembly, where a wrong answer is nearly impossible to attribute.

Writing them found the collector fine and its *caller* wrong. `expose_allocation` emitted the heap-exhaustion check in the body of the `let` that binds the allocation:

```
(let ([v (allocate 2 ...)])          ; bumps free_ptr
  (begin (if (< (+ free_ptr 24) fromspace_end) (void) (collect 24))
         (vector-set! v 0 x) ...))   ; check happens here, too late
```

which compiles to exactly what it says:

```
    movq free_ptr(%rip), %r11
    addq $24, free_ptr(%rip)      ← allocate, unconditionally
    ...
    movq free_ptr(%rip), %rcx     ← only now, is there room?
    cmpq %rdx, %rcx
```

Two consequences. The object can be placed past `fromspace_end`. And if the check does fire, `collect` runs when the fresh object is not yet on the root stack — so it is not copied, and `free_ptr` is reset back over it.

It never manifested. The allocator's slack absorbed the overflow; shrinking the heap to 48 bytes still produced correct answers; AddressSanitizer cannot see it because the offending writes come from generated assembly it never instrumented. It is a latent memory-corruption bug that testing could not have provoked and that only reading the emitted code reveals — found because writing a test for a stated property forced someone to look at whether the property held.

The ten collector tests now cover relocation, reclamation, transitive tracing, shared children via forwarding pointers, cycles, non-pointer slots holding integers that look like addresses, tagged immediates, tag preservation across relocation, and stability over repeated collections.

---

## What is and is not proved

The credibility of everything above depends on this section being exact.

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
| Cast insertion mentions only flat types | **proved** |
| A failed projection reaches `Exit` (syntactically) | **proved** |
| Liveness, interference and colouring soundness (on models) | **proved** |
| A-normal form after RCO | *stated, `sorry`* |
| No shadowing after `uniquify` | *stated, `sorry`* |
| Uniquify's counter is monotone | *stated, `sorry`* |
| **Semantics preservation, for any pass** | **not modelled** |
| **Type progress and preservation** | **not modelled** |
| **The entire backend** (explicate, select, assign homes, patch, prelude) | **not modelled** |

Thirty-three theorems: thirty proved, three with tracked `sorry`s, zero vacuous, enforced on every build.

Two honest qualifications about what "proved" means here.

**These are syntactic properties of a Lean model, not of the C++.** The model is a hand-written mirror of the passes; nothing checks that it matches the C++, and where the two have diverged — `revealCasts` hard-codes a temporary name where the C++ generates fresh ones — the file says so. Proving things about the model catches design errors, not transcription errors. Transcription is what the 344 tests and the differential harness are for.

**Semantics preservation is absent, and that is the interesting boundary.** `shrink_preserves_semantics` cannot be *stated* without an operational semantics, and for this language that is not a small addition. The signature people reach for, `eval : Env → Expr → Option Value`, is insufficient in four separate ways: `set!` and `vectorSet` mutate, so it needs a store; `read` is I/O, so it needs an input stream; `while` and general application need not terminate, so Lean demands a fuel parameter; and a trapped dynamic type error is a third outcome, distinct from both "value" and "stuck". That is roughly 400 lines and several days before a single theorem, plus a fuel-monotonicity lemma that every subsequent proof depends on. Then each preservation theorem is days to weeks on its own, and `uniquify`'s needs a renaming bisimulation.

So verification cost is superlinear in language features, the syntactic properties are the affordable tier, and this project stopped there. Saying so is more useful than a table of thirty-five green ticks, and it is the difference between a verification claim you can rely on and one you cannot.

Phase 9 of the roadmap — gradual typing, with proxy objects for higher-order casts — was **not attempted**, for the same reason: the remaining effort was worth more spent making eight phases true than making nine phases green.

---

## What generalises

Little of this is about compilers, and none of it is about Lean.

**A green check is a claim about whatever was actually checked.** Twenty theorems, one unimported module, one test suite that asserted structure instead of behaviour. In each case the signal was intact and the referent was missing. The failure is not in the checking tool; it is in the gap between what the tool verified and what the name of the check implied.

**Prefer checks on the shape of your claims, not just more claims.** Fixing twenty theorems is worth one commit. `Hygiene.lean` is worth the project, because it makes the failure mode loud. The generalisation is cheap in any codebase: a test that asserts nothing, a mock that returns the value under test, an alert on a metric with no emitter — all are detectable mechanically, and all are invisible to review.

**Independent implementations of the same specification are the highest-yield test you can write.** The differential harness is forty lines of shell and found more than every other regime combined, because the interpreter and the compiled binary share almost nothing. Wherever two paths compute the same thing — a cache and its source, an optimised and a reference implementation, a migration's old and new query — that comparison is nearly free and answers a question no structural assertion can.

**State the boundary of what you have verified, precisely.** The table above is shorter than the one it replaced, and it is worth vastly more, because every row can be checked. An overclaimed guarantee is worse than an absent one: it stops people looking.

---

## Building it

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
ctest --test-dir build --output-on-failure   # 32 binaries, 344 cases
./tests/run_differential.sh                  # 50 programs, both execution paths

cd lean && lake build                        # proofs + the hygiene checker
./scripts/check_honesty.sh                   # fast pre-commit gate
```

Compiling and running a program:

```bash
./build/src/mc input.mc -o output.s
gcc -o output output.s build/src/libmc_runtime.a -lm
echo "52 10" | ./output; echo $?            # the value is the exit status

echo "52 10" | ./build/src/mc -i input.mc   # or interpret it
```

```
let x = read();
let y = read();
x + -(y)
```

With input `52` and `10`, produces `42`.

CI runs the cold build — which is where `clang-tidy`'s function-size gate actually fires, since an incremental build skips untouched files — plus the tests, the differential harness, and `lake build` including the proof-hygiene checker.

---

## Reference

Siek, J. G. (2023). *Essentials of Compilation: An Incremental Approach in Racket*. MIT Press. ISBN 9780262047760.

See [ROADMAP.md](ROADMAP.md) for the per-phase plan, including what was declined and why.
