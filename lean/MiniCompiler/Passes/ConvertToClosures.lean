import MiniCompiler.Passes.FreeVars

/-!
# Closure Conversion — L_Lambda (Phase 7)

Lift lambdas to top-level defs, capturing their free variables into a closure
tuple, and route applications through the closure's code pointer. Mirrors
src/passes/convert_to_closures.cpp.
-/

namespace MiniCompiler

/-- Free vars a lambda must capture: body free vars minus the lambda's params. -/
def lambdaCaptures (params : List (String × Ty)) (body : Expr) : List String :=
  (freeVars body).filter (fun n => !(params.map (·.1)).contains n)

/-- Build a closure tuple: code pointer (funRef, arity+1) followed by the
    captured free vars as `var` elements. -/
def buildClosure (name : String) (arity : Nat) (fv : List String) : Expr :=
  .closure arity (.funRef name (arity + 1) :: fv.map .var)

/-- Every free variable of a lambda body that is not one of its parameters is
    captured by the closure. -/
theorem free_vars_subset_captured (params : List (String × Ty)) (body : Expr)
    (n : String) (hfv : n ∈ freeVars body) (hp : n ∉ params.map (·.1)) :
    n ∈ lambdaCaptures params body := by
  simp [lambdaCaptures, List.mem_filter, hfv, hp]

/-- The closure tuple is a code pointer followed by exactly the captured names,
    so every capture is present as a `var` element. -/
theorem closure_tuple_has_all_fvs (name : String) (arity : Nat)
    (fv : List String) (n : String) (h : n ∈ fv) :
    ∃ elems, buildClosure name arity fv
      = .closure arity (.funRef name (arity + 1) :: elems)
      ∧ (Expr.var n) ∈ elems :=
  ⟨fv.map .var, rfl, List.mem_map_of_mem h⟩

-- NOTE: `closure_conversion_preserves_semantics` is deliberately absent. It
-- cannot be stated without an operational semantics for L_Lambda, and the
-- closure-conversion pass itself is not modelled in Lean (only its two helper
-- functions above are). A `True`-valued placeholder previously stood here; see
-- the "What is and is not proved" section of the README.

end MiniCompiler
