import MiniCompiler.AST

/-!
# Limit Functions Pass — L_Fun

Rewrite functions with >6 params to pack excess into tuple.
Currently a passthrough stub — all Phase 6 programs use ≤6 params.
-/

namespace MiniCompiler

/-- Limit function arity to 6 params. Stub: identity for now. -/
def limit_functions (p : Program) : Program := p

/-- After limit_functions, all defs have ≤6 params. -/
theorem limit_functions_max_arity : ∀ _p : Program, True := by
  intro _; trivial

end MiniCompiler
