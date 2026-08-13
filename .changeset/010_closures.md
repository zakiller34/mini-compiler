---
"mini-compiler": minor
---

Phase 7: lexically scoped functions & closures (L_Lambda). Adds `lambda`
anonymous functions and `procedure_arity`, assignment conversion (boxing of
mutable captured variables), and closure conversion (lambdas lifted to
top-level functions, free variables captured in closure tuples, applications
routed through the closure code pointer). The interpreter now shares mutable
state through captured closures, so `-i` matches compiled output. Adds unit
tests (free_vars, convert_assignments, closure_conversion), Z3 predicate tests
(closure captures exactly the free variables), pipeline lowering tests, and
`tests/programs/phase7/` end-to-end programs.
