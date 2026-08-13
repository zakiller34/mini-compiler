import MiniCompiler.AST

/-!
# Limit Functions Pass — L_Fun

Rewrite functions with >6 params to pack excess into tuple.
Currently a passthrough stub — all Phase 6 programs use ≤6 params.
-/

namespace MiniCompiler

/-- Limit function arity to 6 params. Identity stub: the Lean model lags the
    C++ pass, which does implement the packing (`src/passes/limit_functions.cpp`,
    371 lines). -/
def limit_functions (p : Program) : Program := p

-- NOTE: no `limit_functions_max_arity` theorem here, deliberately.
--
-- The obvious statement — `∀ d ∈ (limit_functions p).defs, d.params.length ≤ 6`
-- — is FALSE of this identity stub. Writing it with a `sorry` would leave the
-- repository formally asserting something untrue, which is strictly worse than
-- the `True`-valued placeholder that used to stand here: anyone who later
-- discharged that `sorry` would have derived `False`. The honest options are to
-- port the real pass or to state nothing. Until the pass is ported, nothing.

end MiniCompiler
