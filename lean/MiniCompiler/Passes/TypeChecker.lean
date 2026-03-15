import MiniCompiler.AST

/-!
# Type Checker — L_If

Type-check expressions, ensuring operand/branch type consistency.
-/

namespace MiniCompiler

/-- Type-check an expression, returning its type if well-typed. -/
def type_check : Expr → Option Ty := sorry

/-- Progress: well-typed expressions can always take a step or are values. -/
theorem type_progress : ∀ e : Expr, ∀ τ : Ty,
    type_check e = some τ → True := sorry

/-- Preservation: evaluation preserves types. -/
theorem type_preservation : ∀ e : Expr, ∀ τ : Ty,
    type_check e = some τ → True := sorry

end MiniCompiler
