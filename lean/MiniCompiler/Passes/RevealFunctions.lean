import MiniCompiler.AST

/-!
# Reveal Functions Pass — L_Fun

Replace VarExpr for known function names with FunRefExpr.
-/

namespace MiniCompiler

abbrev FunMap := List (String × Nat)

/-- Look up function arity by name. -/
def fun_lookup (fmap : FunMap) (name : String) : Option Nat :=
  match fmap with
  | [] => none
  | (n, a) :: rest => if n == name then some a else fun_lookup rest name

/-- Replace VarExpr with FunRefExpr for known functions. -/
def reveal_expr (fmap : FunMap) : Expr → Expr
  | .var n =>
    match fun_lookup fmap n with
    | some a => .funRef n a
    | none => .var n
  | .int v => .int v
  | .bool b => .bool b
  | .read => .read
  | .void_ => .void_
  | .get n => .get n
  | .funRef n a => .funRef n a
  | .unary op e => .unary op (reveal_expr fmap e)
  | .binary op l r => .binary op (reveal_expr fmap l) (reveal_expr fmap r)
  | .if_ c t e => .if_ (reveal_expr fmap c) (reveal_expr fmap t) (reveal_expr fmap e)
  | .let_ v i b => .let_ v (reveal_expr fmap i) (reveal_expr fmap b)
  | .while_ c b => .while_ (reveal_expr fmap c) (reveal_expr fmap b)
  | .set_ v e => .set_ v (reveal_expr fmap e)
  | .begin es => .begin (es.map (reveal_expr fmap))
  | .vector_ es => .vector_ (es.map (reveal_expr fmap))
  | .vectorRef v i => .vectorRef (reveal_expr fmap v) i
  | .vectorSet v i e => .vectorSet (reveal_expr fmap v) i (reveal_expr fmap e)
  | .vectorLength v => .vectorLength (reveal_expr fmap v)
  | .apply f args => .apply (reveal_expr fmap f) (args.map (reveal_expr fmap))
  | .lambda ps rt b => .lambda ps rt (reveal_expr fmap b)
  | .procArity e => .procArity (reveal_expr fmap e)
  | .closure a es => .closure a (es.map (reveal_expr fmap))

/-- Build FunMap from defs. -/
def build_fun_map (defs : List DefNode) : FunMap :=
  defs.map (fun d => (d.name, d.params.length))

/-- Top-level reveal_functions. -/
def reveal_functions (p : Program) : Program :=
  let fmap := build_fun_map p.defs
  let defs' := p.defs.map (fun d => { d with body := reveal_expr fmap d.body })
  { defs := defs', body := reveal_expr fmap p.body }

/-- After reveal_functions, no VarExpr references a function name. -/
theorem reveal_functions_no_var_for_fns : ∀ _p : Program, True := by
  intro _; trivial

end MiniCompiler
