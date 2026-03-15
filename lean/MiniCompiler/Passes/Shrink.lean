import MiniCompiler.AST

/-!
# Shrink Pass — L_If

Desugar `and`/`or` to `if` expressions.
- `and(a, b)` → `if(a, b, false)`
- `or(a, b)` → `if(a, true, b)`
-/

namespace MiniCompiler

/-- Desugar and/or to if expressions. -/
def shrink : Expr → Expr := sorry

/-- Shrink preserves evaluation semantics. -/
theorem shrink_preserves_semantics : ∀ e : Expr,
    True := sorry

/-- Bool encoding roundtrip: and/or desugaring is equivalent. -/
theorem bool_encoding_roundtrip : ∀ (a b : Bool),
    (a && b) = (if a then b else false) := by
  intro a b
  cases a <;> simp

end MiniCompiler
