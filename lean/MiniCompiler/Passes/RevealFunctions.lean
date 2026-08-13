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
  -- Phase 8 (L_Any): these carry sub-expressions and must be traversed, not
  -- treated as leaves. Omitting them left this module failing to elaborate.
  | .inject e t => .inject (reveal_expr fmap e) t
  | .project e t => .project (reveal_expr fmap e) t
  | .typePred p e => .typePred p (reveal_expr fmap e)
  | .anyVectorRef v i => .anyVectorRef (reveal_expr fmap v) (reveal_expr fmap i)
  | .anyVectorSet v i x =>
    .anyVectorSet (reveal_expr fmap v) (reveal_expr fmap i) (reveal_expr fmap x)
  | .anyVectorLength v => .anyVectorLength (reveal_expr fmap v)
  | .makeAny e tag => .makeAny (reveal_expr fmap e) tag
  | .tagOfAny e => .tagOfAny (reveal_expr fmap e)
  | .valueOf e t => .valueOf (reveal_expr fmap e) t
  | .exit_ => .exit_

/-- Build FunMap from defs. -/
def build_fun_map (defs : List DefNode) : FunMap :=
  defs.map (fun d => (d.name, d.params.length))

/-- Top-level reveal_functions. -/
def reveal_functions (p : Program) : Program :=
  let fmap := build_fun_map p.defs
  let defs' := p.defs.map (fun d => { d with body := reveal_expr fmap d.body })
  { defs := defs', body := reveal_expr fmap p.body }

/-- A failed lookup means the name is not a known function. -/
theorem fun_lookup_none (fmap : FunMap) (n : String)
    (h : fun_lookup fmap n = none) : (fmap.map (·.1)).contains n = false := by
  induction fmap with
  | nil => rfl
  | cons hd tl ih =>
    simp only [fun_lookup] at h
    split at h
    · exact absurd h (by simp)
    · rename_i hne
      simp only [List.map_cons, List.contains_cons, ih h, Bool.or_false]
      simp only [beq_iff_eq] at hne ⊢
      exact beq_eq_false_iff_ne.mpr (fun hc => hne hc.symm)

/-- After reveal_functions, no `var` node references a function name — every
    such reference has become a `funRef`. -/
theorem reveal_functions_no_var_for_fns (fmap : FunMap) (e : Expr) :
    noVarIn (fmap.map (·.1)) (reveal_expr fmap e) = true := by
  induction e using Expr.rec
    (motive_2 := fun es =>
      noVarInL (fmap.map (·.1)) (es.map (reveal_expr fmap)) = true) with
  | var n =>
    cases h : fun_lookup fmap n with
    | none =>
      simp only [reveal_expr, h, noVarIn, fun_lookup_none fmap n h,
        Bool.not_false]
    | some a => simp [reveal_expr, noVarIn, h]
  | nil => simp [noVarInL]
  | cons e es ih ihs => simp [noVarInL, ih, ihs]
  | _ => simp_all [reveal_expr, noVarIn]

end MiniCompiler
