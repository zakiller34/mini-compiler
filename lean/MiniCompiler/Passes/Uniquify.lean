import MiniCompiler.AST

/-!
# Uniquify Pass — L_While

Rename all let-bound variables to unique names.
Handles Phase 4 nodes (while, set!, begin, void, get).
-/

namespace MiniCompiler

abbrev RenameEnv := List (String × String)

/-- Look up rename in environment. -/
def rename_lookup (env : RenameEnv) (name : String) : String :=
  match env with
  | [] => name
  | (n, r) :: rest => if n == name then r else rename_lookup rest name

/-- Uniquify expression, renaming let-bound vars. -/
def uniquify_expr (env : RenameEnv) (counter : Nat) : Expr → Expr × Nat
  | .int v => (.int v, counter)
  | .bool b => (.bool b, counter)
  | .var n => (.var (rename_lookup env n), counter)
  | .read => (.read, counter)
  | .unary op e =>
    let (e', c) := uniquify_expr env counter e
    (.unary op e', c)
  | .binary op l r =>
    let (l', c1) := uniquify_expr env counter l
    let (r', c2) := uniquify_expr env c1 r
    (.binary op l' r', c2)
  | .if_ c t e =>
    let (c', c1) := uniquify_expr env counter c
    let (t', c2) := uniquify_expr env c1 t
    let (e', c3) := uniquify_expr env c2 e
    (.if_ c' t' e', c3)
  | .let_ v i b =>
    let newName := v ++ "." ++ toString counter
    let (i', c1) := uniquify_expr env (counter + 1) i
    let (b', c2) := uniquify_expr ((v, newName) :: env) c1 b
    (.let_ newName i' b', c2)
  | .while_ c b =>
    let (c', c1) := uniquify_expr env counter c
    let (b', c2) := uniquify_expr env c1 b
    (.while_ c' b', c2)
  | .set_ v e =>
    let (e', c) := uniquify_expr env counter e
    (.set_ (rename_lookup env v) e', c)
  | .begin es =>
    let (es', c) := es.foldl (fun (acc : List Expr × Nat) e =>
      let (e', c') := uniquify_expr env acc.2 e
      (acc.1 ++ [e'], c')) ([], counter)
    (.begin es', c)
  | .void_ => (.void_, counter)
  | .get n => (.get (rename_lookup env n), counter)
  | .vector_ es =>
    let (es', c) := es.foldl (fun (acc : List Expr × Nat) e =>
      let (e', c') := uniquify_expr env acc.2 e
      (acc.1 ++ [e'], c')) ([], counter)
    (.vector_ es', c)
  | .vectorRef v i =>
    let (v', c) := uniquify_expr env counter v
    (.vectorRef v' i, c)
  | .vectorSet v i e =>
    let (v', c1) := uniquify_expr env counter v
    let (e', c2) := uniquify_expr env c1 e
    (.vectorSet v' i e', c2)
  | .vectorLength v =>
    let (v', c) := uniquify_expr env counter v
    (.vectorLength v', c)
  | .apply f args =>
    let (f', c1) := uniquify_expr env counter f
    let (args', c2) := args.foldl (fun (acc : List Expr × Nat) a =>
      let (a', c') := uniquify_expr env acc.2 a
      (acc.1 ++ [a'], c')) ([], c1)
    (.apply f' args', c2)
  | .funRef n a => (.funRef n a, counter)
  | .lambda ps rt b =>
    let (b', c) := uniquify_expr env counter b
    (.lambda ps rt b', c)
  | .procArity e =>
    let (e', c) := uniquify_expr env counter e
    (.procArity e', c)
  | .closure a es =>
    let (es', c) := es.foldl (fun (acc : List Expr × Nat) e =>
      let (e', c') := uniquify_expr env acc.2 e
      (acc.1 ++ [e'], c')) ([], counter)
    (.closure a es', c)
  -- Phase 8 (L_Any). These bind no variables, but they *contain* variables, so
  -- a catch-all `| e => (e, counter)` left them un-renamed.
  | .inject e t =>
    let (e', c) := uniquify_expr env counter e
    (.inject e' t, c)
  | .project e t =>
    let (e', c) := uniquify_expr env counter e
    (.project e' t, c)
  | .typePred p e =>
    let (e', c) := uniquify_expr env counter e
    (.typePred p e', c)
  | .anyVectorRef v i =>
    let (v', c1) := uniquify_expr env counter v
    let (i', c2) := uniquify_expr env c1 i
    (.anyVectorRef v' i', c2)
  | .anyVectorSet v i x =>
    let (v', c1) := uniquify_expr env counter v
    let (i', c2) := uniquify_expr env c1 i
    let (x', c3) := uniquify_expr env c2 x
    (.anyVectorSet v' i' x', c3)
  | .anyVectorLength v =>
    let (v', c) := uniquify_expr env counter v
    (.anyVectorLength v', c)
  | .makeAny e tag =>
    let (e', c) := uniquify_expr env counter e
    (.makeAny e' tag, c)
  | .tagOfAny e =>
    let (e', c) := uniquify_expr env counter e
    (.tagOfAny e', c)
  | .valueOf e t =>
    let (e', c) := uniquify_expr env counter e
    (.valueOf e' t, c)
  | .exit_ => (.exit_, counter)

/-- Top-level uniquify. -/
def uniquify (p : Program) : Program :=
  let (body', _) := uniquify_expr [] 1 p.body
  { body := body' }

-- Every name bound by a `let`, in source order.
mutual
/-- Names bound by a `let` anywhere in `e`. -/
def letBoundNames : Expr → List String
  | .let_ v i b => v :: letBoundNames i ++ letBoundNames b
  | .unary _ e => letBoundNames e
  | .binary _ l r => letBoundNames l ++ letBoundNames r
  | .if_ c t e => letBoundNames c ++ letBoundNames t ++ letBoundNames e
  | .while_ c b => letBoundNames c ++ letBoundNames b
  | .set_ _ e => letBoundNames e
  | .begin es => letBoundNamesL es
  | .vector_ es => letBoundNamesL es
  | .vectorRef v _ => letBoundNames v
  | .vectorSet v _ e => letBoundNames v ++ letBoundNames e
  | .vectorLength v => letBoundNames v
  | .apply f args => letBoundNames f ++ letBoundNamesL args
  | .lambda _ _ b => letBoundNames b
  | .procArity e => letBoundNames e
  | .closure _ es => letBoundNamesL es
  | .inject e _ => letBoundNames e
  | .project e _ => letBoundNames e
  | .typePred _ e => letBoundNames e
  | .anyVectorRef v i => letBoundNames v ++ letBoundNames i
  | .anyVectorSet v i x => letBoundNames v ++ letBoundNames i ++ letBoundNames x
  | .anyVectorLength v => letBoundNames v
  | .makeAny e _ => letBoundNames e
  | .tagOfAny e => letBoundNames e
  | .valueOf e _ => letBoundNames e
  | _ => []

/-- Pointwise lifting of `letBoundNames` to a list of sub-expressions. -/
def letBoundNamesL : List Expr → List String
  | [] => []
  | e :: es => letBoundNames e ++ letBoundNamesL es
end

/-- After uniquify, no two `let` bindings share a name — so no scope shadows
    another and every variable reference resolves unambiguously.

    TRACKED `sorry`. The proof needs three ingredients: monotonicity of the
    counter (see `uniquify_counter_mono` below), the invariant that every
    generated name carries a suffix drawn from the half-open interval consumed
    by that subtree, and injectivity of `v ++ "." ++ toString k` in `k`. The
    last is the hard one without Mathlib's `String`/`Nat.toString` lemmas.
    Listed in `SORRY_ALLOWLIST`. -/
theorem uniquify_no_shadowing (p : Program) :
    (letBoundNames (uniquify p).body).Nodup := by
  sorry

/-- The counter never decreases: a subtree only ever consumes fresh numbers.

    TRACKED `sorry`, and the first of the three ingredients above. The
    structural cases are immediate; what it needs is a companion lemma for the
    `foldl` used by `begin`/`vector_`/`apply`/`closure`, stating that the fold
    is monotone in its accumulator. Listed in `SORRY_ALLOWLIST`. -/
theorem uniquify_counter_mono (e : Expr) :
    ∀ (env : RenameEnv) (c : Nat), c ≤ (uniquify_expr env c e).2 := by
  sorry

end MiniCompiler
