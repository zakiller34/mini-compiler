import MiniCompiler.AST

/-!
# Free Variables — L_Lambda (Phase 7)

Free variables of an expression: names used via var/get/set! minus names bound
by let / lambda params along the path. Mirrors src/passes/free_vars.cpp.
-/

namespace MiniCompiler

/-- Names free in `e` (scoped: let and lambda params are binders). -/
def freeVars : Expr → List String
  | .var n => [n]
  | .get n => [n]
  | .set_ n e => n :: freeVars e
  | .let_ v i b => freeVars i ++ (freeVars b).filter (· ≠ v)
  | .lambda ps _ b =>
    (freeVars b).filter (fun n => !(ps.map (·.1)).contains n)
  | .unary _ e => freeVars e
  | .binary _ l r => freeVars l ++ freeVars r
  | .if_ c t e => freeVars c ++ freeVars t ++ freeVars e
  | .while_ c b => freeVars c ++ freeVars b
  | .begin es => es.flatMap freeVars
  | .vector_ es => es.flatMap freeVars
  | .vectorRef v _ => freeVars v
  | .vectorSet v _ e => freeVars v ++ freeVars e
  | .vectorLength v => freeVars v
  | .apply f args => freeVars f ++ args.flatMap freeVars
  | .procArity e => freeVars e
  | .closure _ es => es.flatMap freeVars
  -- Phase 8 (L_Any). Missing these under-approximated the free-variable set,
  -- which would silently drop captures during closure conversion.
  | .inject e _ => freeVars e
  | .project e _ => freeVars e
  | .typePred _ e => freeVars e
  | .anyVectorRef v i => freeVars v ++ freeVars i
  | .anyVectorSet v i x => freeVars v ++ freeVars i ++ freeVars x
  | .anyVectorLength v => freeVars v
  | .makeAny e _ => freeVars e
  | .tagOfAny e => freeVars e
  | .valueOf e _ => freeVars e
  | _ => []

/-- `let` binds its variable: it is not free in the result unless the
    initialiser mentions it. -/
theorem freeVars_let_binder (v : String) (i b : Expr) (h : v ∉ freeVars i) :
    v ∉ freeVars (.let_ v i b) := by
  simp [freeVars, List.mem_filter, h]

/-- Lambda parameters are bound, hence not free in the lambda. -/
theorem freeVars_lambda_params (ps : List (String × Ty)) (rt : Ty) (b : Expr)
    (n : String) (h : n ∈ ps.map (·.1)) : n ∉ freeVars (.lambda ps rt b) := by
  simp [freeVars, List.mem_filter, h]

end MiniCompiler
