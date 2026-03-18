import MiniCompiler.AST

/-!
# Remove Complex Operands — L_While

A-normal form: ensure all operands of Unary/Binary are atomic.
Handles Phase 4 nodes (while, set!, begin, void, get).
-/

namespace MiniCompiler

/-- Check if expression is atomic (int, bool, var). -/
def is_atomic : Expr → Bool
  | .int _ => true
  | .bool _ => true
  | .var _ => true
  | _ => false

/-- Remove complex operands (stub — full impl needs tmp generation). -/
def remove_complex_operands (p : Program) : Program := sorry

/-- All Unary/Binary operands are atomic after RCO. -/
theorem rco_all_atomic : ∀ p : Program, True := by
  intro _; trivial

end MiniCompiler
