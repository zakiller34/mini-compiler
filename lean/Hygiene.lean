import MiniCompiler
import Lean

/-!
# Proof hygiene checker

A green `lake build` says every proof elaborates. It does *not* say the proofs
are worth anything: `theorem foo : ∀ _p : Program, True := by trivial` is
perfectly valid Lean and perfectly worthless. This repository shipped twenty
such theorems before anyone noticed, because the names read like real claims
and nothing mechanical was checking the *shape* of what was claimed.

This module is that check. It runs at elaboration time over every declaration
in the `MiniCompiler` modules and fails the build on:

1. **Vacuous conclusions** — a theorem whose conclusion (after stripping
   binders) is `True`, contains `True` as a disjunct, or is a reflexive
   equation `a = a`. All three typecheck; none of them assert anything.
2. **Unlisted `sorry`** — any declaration depending on `sorryAx` that is not
   named in `sorryAllowlist` below. The allowlist holds *names*, not a count,
   so a `sorry` that is fixed in one place and reintroduced in another cannot
   hide behind an unchanged total.
3. **`native_decide`** — any declaration depending on `Lean.ofReduceBool`.
   That tactic will happily close goals the kernel cannot check, which is not
   a trade this repository is willing to make.
-/

open Lean Meta Elab Command

/-- Declarations permitted to depend on `sorryAx`. Checked-in baseline.

Each entry is genuine, *stated* proof debt: a true proposition whose proof is
not yet written, or a definition not yet ported from C++. Adding a name here is
a deliberate act that shows up in review. -/
def sorryAllowlist : List Name :=
  [ -- Backend passes with no Lean model at all (`def … := sorry`).
    -- NB: root namespace — SelectInstructions.lean opens no namespace.
    `select_instructions,
    `MiniCompiler.assign_homes,
    `MiniCompiler.patch_instructions,
    `MiniCompiler.generate_prelude_conclusion,
    `MiniCompiler.explicate_control,
    `MiniCompiler.remove_complex_operands,
    -- True statements whose proofs are not yet written.
    `MiniCompiler.rco_all_atomic,
    `MiniCompiler.uniquify_no_shadowing,
    `MiniCompiler.uniquify_counter_mono ]

/-- Does `True` occur anywhere in this term? -/
partial def hasTrue : Lean.Expr → Bool
  | .const ``True _ => true
  | .app f a => hasTrue f || hasTrue a
  | _ => false

/-- A conclusion with no content: `True`, a disjunction with a `True` side, or
    a reflexive equation. -/
def vacuousBody (b : Lean.Expr) : Bool :=
  b.isConstOf ``True
  || (b.isAppOfArity ``Or 2 && (hasTrue b.appFn!.appArg! || hasTrue b.appArg!))
  || (b.isAppOfArity ``Eq 3 && b.appFn!.appArg! == b.appArg!)

/-- Is this declaration one of ours?

Filter by *module*, not by namespace: `AST.lean` opens no namespace at all and
several pass modules declare theorems at the root, so a `MiniCompiler.`-prefix
test would silently skip them — exactly the kind of blind spot this file
exists to prevent. -/
def ourModule (env : Environment) (n : Name) : Bool :=
  match env.getModuleIdxFor? n with
  | some idx => (env.allImportedModuleNames[idx.toNat]!).getRoot == `MiniCompiler
  | none => false

run_cmd do
  let env ← getEnv
  let mut vacuous : Array Name := #[]
  let mut unlistedSorry : Array Name := #[]
  let mut nativeDecide : Array Name := #[]
  let mut proved := 0
  for (nm, ci) in env.constants.toList do
    -- `isInternalDetail` filters auto-generated equation lemmas; without it
    -- this scans several hundred derived declarations instead of the real ones.
    if ourModule env nm && !nm.isInternalDetail then
      let ax ← collectAxioms nm
      if ax.contains ``sorryAx && !sorryAllowlist.contains nm then
        unlistedSorry := unlistedSorry.push nm
      if ax.contains ``Lean.ofReduceBool then
        nativeDecide := nativeDecide.push nm
      match ci with
      | .thmInfo ti =>
        let v ← liftTermElabM <| Meta.forallTelescopeReducing ti.type fun _ body =>
          return vacuousBody (← whnf body)
        if v then vacuous := vacuous.push nm
        else if !ax.contains ``sorryAx then proved := proved + 1
      | _ => pure ()
  unless vacuous.isEmpty && unlistedSorry.isEmpty && nativeDecide.isEmpty do
    throwError m!"PROOF HYGIENE FAILURE\n\
      \n  vacuous conclusions ({vacuous.size}): {vacuous}\
      \n  unlisted sorry ({unlistedSorry.size}): {unlistedSorry}\
      \n  native_decide ({nativeDecide.size}): {nativeDecide}\
      \n\nA theorem whose conclusion is `True` proves nothing. State the real \
      proposition; if you cannot prove it yet, leave a `sorry` and add the name \
      to `sorryAllowlist` in lean/Hygiene.lean."
  -- NB: `checked` counts every theorem-valued declaration in these modules,
  -- which includes ones Lean generates from `deriving` and from structural
  -- recursion. It is a coverage figure, not a count of hand-written proofs;
  -- for that, count `^theorem` in the sources.
  logInfo m!"proof hygiene: {proved} theorem-valued declarations checked, \
    0 vacuous conclusions, {sorryAllowlist.length} allowlisted sorry"
