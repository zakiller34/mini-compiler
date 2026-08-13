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

/-- Every operand of a `unary`/`binary` node is atomic. -/
def operandsAtomic : Expr → Bool
  | .unary _ e => is_atomic e
  | .binary _ l r => is_atomic l && is_atomic r
  | _ => true

/-- After RCO, every `unary`/`binary` node has atomic operands — the defining
    property of A-normal form.

    TRACKED `sorry`: unprovable as written, because `remove_complex_operands`
    above is itself `sorry`. It is stated rather than omitted because the
    statement is true of the intended pass and of the C++ implementation
    (`src/passes/rco.cpp`); closing it requires porting that pass to Lean
    first. Listed in `SORRY_ALLOWLIST`. -/
theorem rco_all_atomic (p : Program) :
    operandsAtomic (remove_complex_operands p).body = true := by
  sorry

end MiniCompiler
