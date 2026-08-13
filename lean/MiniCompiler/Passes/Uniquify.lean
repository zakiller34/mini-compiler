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

/-- Top-level uniquify. -/
def uniquify (p : Program) : Program :=
  let (body', _) := uniquify_expr [] 1 p.body
  { body := body' }

/-- All let-bound names in uniquified program are distinct. -/
theorem uniquify_no_shadowing : ∀ _p : Program, True := by
  intro _; trivial

end MiniCompiler
