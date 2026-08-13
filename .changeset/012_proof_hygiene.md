---
"mini-compiler": minor
---

Lean development made honest, and mechanically kept that way.

Twenty of the thirty-five Lean theorems asserted nothing: they were stated as
`∀ _p : Program, True := by trivial`, which typechecks and proves nothing. Two
more were misleading rather than empty — `tagof_injective_on_flat` concluded
`… ∨ True`, and `project_inject_roundtrip` concluded `tagof t = tagof t`.

Writing the real statements exposed a genuine modelling bug: seven analyses
(`shrink`, `uniquify`, `replace_vars`, `collect_mutable_vars`, `freeVars`,
`collectAssigned`, `collectCaptured`) ended in a catch-all that treated the
Phase 8 `L_Any` nodes as leaves, even though those nodes carry sub-expressions.
`revealCasts` did not recurse at all. All are fixed; `collect_mutable_vars` also
gained the `lambda` case it was missing, so a `set!` inside a lambda body is now
seen.

- `RevealFunctions.lean` had not compiled since Phase 8 landed. It was invisible
  because `MiniCompiler.lean` never imported it and the lakefile had no `globs`,
  so `lake build` was green while two modules were never elaborated. Both fixed.
- `Ty`'s derived `BEq` was replaced by a structural definition: the derived one
  is compiled by well-founded recursion (nested `List Ty`), so `Ty.any == Ty.any`
  was provable by neither `rfl` nor `decide`. This unblocked the one pre-existing
  real `sorry`, `cast_insert_uses_flat_types`.
- New proofs: `shrink_no_and_or`, `reveal_casts_no_casts`,
  `reveal_functions_no_var_for_fns`, `uncover_get_no_var_for_mutable`,
  `free_vars_subset_captured`, `closure_tuple_has_all_fvs`, `toBox_correct`,
  `freeVars_let_binder`, `freeVars_lambda_params`, `tagof_distinguishes_shapes`,
  `lowerProject_else_is_exit`, `fun_lookup_none`, `Ty.beq_refl`.
- Deleted rather than `sorry`d: `limit_functions_max_arity` (false of the
  identity stub) and `explicate_has_start` (about a `sorry`d definition). A false
  statement behind a `sorry` is worse than a vacuous one.
- Removed as unstatable without an operational semantics: every
  `*_preserves_semantics`, plus `type_progress` and `type_preservation`.
- `lean/Hygiene.lean` now fails `lake build` on any vacuous conclusion, any
  `sorry` not in a checked-in name allowlist, or any `native_decide`.
  `lean/scripts/check_honesty.sh` is the fast pre-commit gate and additionally
  verifies every module is imported.

Net: 33 theorems, 0 vacuous, 30 proved, 3 stated with tracked `sorry`.

Also corrects CLAUDE.md, which advertised mathlib4 + cslib dependencies that the
project does not have and has never had.
