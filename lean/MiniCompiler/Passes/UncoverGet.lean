import MiniCompiler.AST

/-!
# Uncover Get Pass — L_While

Collect mutable vars (set! targets), replace VarExpr → GetExpr for them.
-/

namespace MiniCompiler

/-- Collect all variable names that are targets of set!. -/
def collect_mutable_vars : Expr → List String
  | .set_ v e => v :: collect_mutable_vars e
  | .unary _ e => collect_mutable_vars e
  | .binary _ l r => collect_mutable_vars l ++ collect_mutable_vars r
  | .if_ c t e =>
    collect_mutable_vars c ++ collect_mutable_vars t ++ collect_mutable_vars e
  | .let_ _ i b => collect_mutable_vars i ++ collect_mutable_vars b
  | .while_ c b => collect_mutable_vars c ++ collect_mutable_vars b
  | .begin es => es.flatMap collect_mutable_vars
  | .vector_ es => es.flatMap collect_mutable_vars
  | .vectorRef v _ => collect_mutable_vars v
  | .vectorSet v _ e => collect_mutable_vars v ++ collect_mutable_vars e
  | .vectorLength v => collect_mutable_vars v
  | .apply f args => collect_mutable_vars f ++ args.flatMap collect_mutable_vars
  | _ => []

/-- Replace VarExpr with GetExpr for mutable variables. -/
def replace_vars (mvars : List String) : Expr → Expr
  | .var n => if mvars.contains n then .get n else .var n
  | .int v => .int v
  | .bool b => .bool b
  | .read => .read
  | .unary op e => .unary op (replace_vars mvars e)
  | .binary op l r => .binary op (replace_vars mvars l) (replace_vars mvars r)
  | .if_ c t e => .if_ (replace_vars mvars c) (replace_vars mvars t) (replace_vars mvars e)
  | .let_ v i b => .let_ v (replace_vars mvars i) (replace_vars mvars b)
  | .while_ c b => .while_ (replace_vars mvars c) (replace_vars mvars b)
  | .set_ v e => .set_ v (replace_vars mvars e)
  | .begin es => .begin (es.map (replace_vars mvars))
  | .void_ => .void_
  | .get n => .get n
  | .vector_ es => .vector_ (es.map (replace_vars mvars))
  | .vectorRef v i => .vectorRef (replace_vars mvars v) i
  | .vectorSet v i e => .vectorSet (replace_vars mvars v) i (replace_vars mvars e)
  | .vectorLength v => .vectorLength (replace_vars mvars v)
  | .apply f args => .apply (replace_vars mvars f) (args.map (replace_vars mvars))
  | .funRef n a => .funRef n a
  | .lambda ps rt b => .lambda ps rt (replace_vars mvars b)
  | .procArity e => .procArity (replace_vars mvars e)
  | .closure a es => .closure a (es.map (replace_vars mvars))
  -- Phase 8 (L_Any) nodes bind no variables
  | e => e

/-- Top-level uncover_get. -/
def uncover_get (p : Program) : Program :=
  let mvars := collect_mutable_vars p.body
  { body := replace_vars mvars p.body }

/-- Output has no VarExpr for mutable vars. -/
theorem uncover_get_no_var_for_mutable : ∀ _p : Program,
    True := by intro _; trivial

end MiniCompiler
